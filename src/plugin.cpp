// Copyright (c) 2022-2026 Manuel Schneider

#include "bookmarkitem.h"
#include "plugin.h"
#include "ui_configwidget.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <albert/logging.h>
#include <albert/systemutil.h>
#include <albert/widgetsutil.h>
#include <array>
#include <expected>
ALBERT_LOGGING_CATEGORY("chromium")
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std::filesystem;
using namespace std::string_literals;
using namespace std;

namespace {
const auto &CFG_PROFILE_PATH = u"profile_path"_s;
const auto &CFG_MATCH_HOSTNAME = u"match_hostname"_s;
const auto DEF_MATCH_HOSTNAME = false;
const array DATA_DIR_NAMES = {"BraveSoftware"s,
                              "Google/Chrome"s,  // Google Chrome Macos
                              "brave-browser"s,
                              "chromium"s,
                              "google-chrome"s,
                              "vivaldi"s};
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
    auto s = settings();
    match_hostname_ = s->value(CFG_MATCH_HOSTNAME, DEF_MATCH_HOSTNAME).toBool();
    profile_path_ = s->value(CFG_PROFILE_PATH).toString().toStdString();

    const auto profiles = getProfiles();
    if (profiles.empty())
        throw runtime_error(tr("No profiles found.").toStdString());

    if (profile_path_.empty())
        setProfilePath(toQString(profiles.begin()->first));

    indexer.parallel = [p=profile_path_](const bool &abort) { return parseBookmarks(p, abort); };

    indexer.finish = [this]
    {
        bookmarks_ = indexer.takeResult();
        INFO << u"Indexed %1 bookmarks."_s.arg(bookmarks_.size());
        updateIndexItems();
    };

    connect(&fs_watcher_, &QFileSystemWatcher::fileChanged, this, [this] {
        resetBookmarksFileWatch();
        indexer.run();
    });
    resetBookmarksFileWatch();

    indexer.run();
}

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

    bindWidget(ui.checkBox_match_hostname, this, &Plugin::matchHostname, &Plugin::setMatchHostname);

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

    return w;
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

    profile_path_ = v.toStdString();
    settings()->setValue(CFG_PROFILE_PATH, v);
    resetBookmarksFileWatch();
    indexer.parallel = [p=profile_path_](const bool &abort) { return parseBookmarks(p, abort); };
    indexer.run();
}

void Plugin::resetBookmarksFileWatch()
{
    if (!fs_watcher_.files().isEmpty())
        fs_watcher_.removePaths(fs_watcher_.files());
    fs_watcher_.addPath(toQString(profile_path_ / "Bookmarks"));

}
