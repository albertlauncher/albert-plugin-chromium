// Copyright (c) 2022-2026 Manuel Schneider

#include "bookmarkitem.h"
#include "favicons.h"
#include "plugin.h"
#include "ui_configwidget.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <albert/logging.h>
#include <albert/systemutil.h>
#include <albert/widgetsutil.h>
#include <array>
#include <expected>
ALBERT_LOGGING_CATEGORY("chromium")
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std::chrono;
using namespace std::filesystem;
using namespace std::string_literals;
using namespace std;

namespace {

const auto &kFaviconsMtime = u"favicons_mtime"_s;

const auto &CFG_PROFILE_PATH = u"profile_path"_s;
const auto &CFG_MATCH_HOSTNAME = u"match_hostname"_s;
const auto &CFG_SHOW_FAVICONS = u"show_favicons"_s;
const array DATA_DIR_NAMES = {
    "BraveSoftware/Brave-Browser"s,
    "BraveSoftware/Brave-Browser-Beta"s,
    "BraveSoftware/Brave-Browser-Dev"s,
    "BraveSoftware/Brave-Browser-Nightly"s,
    "Chromium"s,
    "Google/Chrome Beta"s,
    "Google/Chrome Dev"s,
    "Google/Chrome SxS"s,
    "Google/Chrome"s,
    "Microsoft Edge Beta"s,
    "Microsoft Edge Canary"s,
    "Microsoft Edge Dev"s,
    "Microsoft Edge"s,
    "Opera Software/Opera GX Stable"s,
    "Opera Software/Opera Stable"s,
    "Vivaldi"s,
    "Yandex/YandexBrowser"s
    "Yandex/YandexBrowserBeta"s,
    "brave-browser"s,
    "brave-browser-beta"s,
    "brave-browser-dev"s,
    "chromium"s,
    "google-chrome"s,
    "google-chrome-beta"s,
    "google-chrome-unstable"s,
    "microsoft-edge"s,
    "microsoft-edge-dev"s,
    "opera"s,
    "opera-gx"s,
    "vivaldi"s,
    "yandex-browser"s,
    "yandex-browser-beta"s
};
}

static expected<map<path, QString>, QString> getProfiles(const path &local_state_file)
{
    QFile file(local_state_file);
    if (!file.open(QIODevice::ReadOnly))
        return unexpected(u"Failed opening file."_s);

    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);

    if (error.error != QJsonParseError::NoError)
        return unexpected(u"Failed parsing JSON. Error: %1"_s.arg(error.errorString()));

    if (!doc.isObject())
        return unexpected(u"Invalid JSON structure."_s);

    else if (const auto root = doc.object();
             !root.contains("profile"_L1))
        return unexpected(u"Missing 'profile' object."_s);

    else if (const auto profiles_val = root.value("profile"_L1);
             !profiles_val.isObject())
        return unexpected(u"Invalid 'profile' object."_s);

    else if (const auto profiles_obj = profiles_val.toObject();
             !profiles_obj.contains("info_cache"_L1))
        return unexpected(u"Missing 'info_cache' object."_s);

    else if (const auto info_cache_val = profiles_obj.value("info_cache"_L1);
             !info_cache_val.isObject())
        return unexpected(u"Invalid 'info_cache' object."_s);

    else if (const auto info_cache_obj = info_cache_val.toObject();
             info_cache_obj.isEmpty())
        return unexpected(u"No profiles found."_s);

    else
    {
        map<path, QString> ret;

        for (const auto &profile_id : info_cache_obj.keys())
            if (const auto profile_val = info_cache_obj[profile_id];
                !profile_val.isObject())
                return unexpected(u"Invalid profile object for id '%1'."_s.arg(profile_id));

            else if (const auto profile_obj = profile_val.toObject();
                     !profile_obj.contains("name"_L1))
                return unexpected(u"Missing 'name' in profile id '%1'."_s.arg(profile_id));

            else if (const auto &name_val = profile_obj.value("name"_L1);
                     !name_val.isString())
                return unexpected(u"Invalid 'name' in profile id '%1'."_s.arg(profile_id));

            else
                ret.emplace(local_state_file.parent_path() / profile_id.toStdString(),
                            name_val.toString());

        return ret;
    }
}

static vector<path> findBrowserDataDirs()
{
#if defined(Q_OS_MAC)
    const auto std_loc = QStandardPaths::GenericDataLocation;
#else
    const auto std_loc = QStandardPaths::GenericConfigLocation;
#endif

    vector<path> data_dir_paths;
    for (const auto &std_path : QStandardPaths::standardLocations(std_loc))
        for (const auto &data_dir_name : DATA_DIR_NAMES)
            if (auto data_dir_path = path(std_path.toStdString()) / data_dir_name;
                exists(data_dir_path))
                data_dir_paths.emplace_back(data_dir_path);
    return data_dir_paths;
}

static map<path, QString> getProfiles()
{
    map<path, QString> profiles;

    for (const auto &data_dir : findBrowserDataDirs())
        if (auto ls = data_dir / "Local State"; exists(ls))
        {
            if (auto p = getProfiles(ls); p.has_value())
                profiles.insert(p->begin(), p->end());
            else
                WARN << p.error();
        }

    return profiles;
}

static void recursiveJsonTreeWalker(const QString &folder_path,
                                    const QJsonObject &json,
                                    vector<shared_ptr<BookmarkItem>> &items,
                                    const bool &abort)
{
    auto name = json[u"name"_s].toString();
    auto type = json[u"type"_s].toString();

    if (type == u"folder"_s)
    {
        // having the full folder path shouldnt be too expensive due to the shared nature of qstring
        QString folder_path_ = folder_path.isEmpty() ? name : folder_path + u" → "_s + name;

        for (const QJsonValueRef &child : json[u"children"_s].toArray())
            if (abort)
                return;
            else
                recursiveJsonTreeWalker(folder_path_, child.toObject(), items, abort);
    }

    else if (type == u"url"_s)
        items.emplace_back(make_shared<BookmarkItem>(json[u"guid"_s].toString(),
                                                     name,
                                                     folder_path,
                                                     json[u"url"_s].toString()));
};

static vector<shared_ptr<BookmarkItem>> parseBookmarks(const path &profile_path, const bool &abort)
{
    vector<shared_ptr<BookmarkItem>> results;

    if (QFile f(profile_path / "Bookmarks"); f.open(QIODevice::ReadOnly))
    {
        const auto doc = QJsonDocument::fromJson(f.readAll());
        const auto roots = doc.object().value(u"roots"_s).toObject();
        for (const auto &root : roots)
            if (root.isObject())
                recursiveJsonTreeWalker({}, root.toObject(), results, abort);
        f.close();
    }
    else
        WARN << "Could not open Bookmarks file:" << f.fileName();

    return results;
}

Plugin::Plugin()
{
    create_directories(cacheLocation());

    auto s = settings();

    match_hostname_ = s->value(CFG_MATCH_HOSTNAME, false).toBool();

    const auto profiles = getProfiles();
    if (profiles.empty())
        throw runtime_error(tr("No profiles found.").toStdString());
    profile_path_ = s->value(CFG_PROFILE_PATH).toString().toStdString();
    if (profile_path_.empty())
        setProfilePath(toQString(profiles.begin()->first));
    connect(&bookmarks_watch_, &QFileSystemWatcher::fileChanged, this, [this] {
        updateBookmarksFileWatch();
        indexer.run();
    });
    updateBookmarksFileWatch();

    indexer.parallel = [p=profile_path_](const bool &abort) { return parseBookmarks(p, abort); };
    indexer.finish = [this] {
        bookmarks_ = indexer.takeResult();
        INFO << u"Indexed %1 bookmarks."_s.arg(bookmarks_.size());
        updateIndexItems();
    };
    indexer.run();

    if (s->value(CFG_SHOW_FAVICONS, true).toBool())
        updateCachedDatabase();
}

Plugin::~Plugin() {}

void Plugin::updateIndexItems()
{
    vector<IndexItem> index_items;
    for (const auto &bookmark : bookmarks_)
    {
        index_items.emplace_back(static_pointer_cast<Item>(bookmark), bookmark->name_);
        if (match_hostname_)
            index_items.emplace_back(static_pointer_cast<Item>(bookmark),
                                     QUrl(bookmark->url_).host());
    }
    setIndexItems(::move(index_items));
}

QWidget *Plugin::buildConfigWidget()
{
    auto *w = new QWidget();
    Ui::ConfigWidget ui;
    ui.setupUi(w);

    // populate profiles checkbox
    for (const auto &[path, name] : getProfiles())
    {
        const auto rel = relative(path, path.parent_path().parent_path());
        const auto title = u"%1 (%2)"_s.arg(name, toQString(rel));
        const auto qpath = toQString(path);

        ui.comboBox_profile->addItem(title, qpath);
        if (path == profile_path_)
            ui.comboBox_profile->setCurrentIndex(ui.comboBox_profile->count()-1);
    }

    connect(ui.comboBox_profile,
            static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this, comboBox_themes=ui.comboBox_profile](int i)
            { setProfilePath(comboBox_themes->itemData(i).toString()); });

    bindWidget(ui.checkBox_show_favicons, this, &Plugin::showFavicons, &Plugin::setShowFavicons);

    bindWidget(ui.checkBox_match_hostname, this, &Plugin::matchHostname, &Plugin::setMatchHostname);

    return w;
}

void Plugin::updateBookmarksFileWatch()
{
    if (!bookmarks_watch_.files().isEmpty())
        bookmarks_watch_.removePaths(bookmarks_watch_.files());
    bookmarks_watch_.addPath(toQString(profile_path_ / "Bookmarks"));
}

void Plugin::updateCachedDatabase()
{
    auto state = this->state();
    const auto db = profile_path_ / "Favicons";
    const auto cached_db = cacheLocation() / "Favicons";
    qint64 last_mtime = state->value(kFaviconsMtime, 0).toLongLong();
    qint64 mtime = duration_cast<seconds>(last_write_time(db).time_since_epoch()).count();

    favicons_.reset();
    if (!exists(cached_db) || mtime > last_mtime)
    {
        error_code ec;
        if (!copy_file(db, cached_db, copy_options::overwrite_existing, ec))
            WARN << "Failed to copy Favicons database to cache:" << ec.message();
        state->setValue(kFaviconsMtime, mtime);
    }
    favicons_ = make_unique<Favicons>(cached_db);
    BookmarkItem::favicons = favicons_.get();
}

bool Plugin::matchHostname() const { return match_hostname_; }

void Plugin::setMatchHostname(bool v)
{
    if (v == match_hostname_)
        return;

    match_hostname_ = v;
    settings()->setValue(CFG_MATCH_HOSTNAME, v);
    indexer.run();
}

QString Plugin::profilePath() const { return toQString(profile_path_); }

void Plugin::setProfilePath(const QString &v)
{
    if (v == toQString(profile_path_))
        return;

    settings()->setValue(CFG_PROFILE_PATH, v);
    profile_path_ = v.toStdString();

    updateBookmarksFileWatch();

    if (showFavicons())
        updateCachedDatabase();

    indexer.parallel = [p=profile_path_](const bool &abort) { return parseBookmarks(p, abort); };
    indexer.run();
}

bool Plugin::showFavicons() const { return favicons_.get(); }

void Plugin::setShowFavicons(bool v)
{
    if (v == showFavicons())
        return;

    settings()->setValue(CFG_SHOW_FAVICONS, v);

    if (v)
        updateCachedDatabase();
    else
    {
        favicons_.reset();
        BookmarkItem::favicons = nullptr;
    }
}
