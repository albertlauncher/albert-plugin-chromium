// Copyright (c) 2022-2025 Manuel Schneider

#include "bookmarkitem.h"
#include "plugin.h"
#include "ui_configwidget.h"
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QStringListModel>
#include <albert/albert.h>
#include <albert/logging.h>
#include <utility>
ALBERT_LOGGING_CATEGORY("chromium")
using namespace Qt::StringLiterals;
using namespace albert::util;
using namespace albert;
using namespace std;

static const auto CFG_BM_PATHS = u"bookmarks_path"_s;
static const auto CFG_INDEX_HOSTNAME = u"indexHostname"_s;
static const bool DEF_INDEX_HOSTNAME = false;

static const QString app_dirs[] = {u"BraveSoftware"_s,
                                   u"Google/Chrome"_s,  // Google Chrome Macos
                                   u"brave-browser"_s,
                                   u"chromium"_s,
                                   u"google-chrome"_s,
                                   u"vivaldi"_s};

static void recursiveJsonTreeWalker(const QString &folder_path,
                                    const QJsonObject &json,
                                    vector<shared_ptr<BookmarkItem>> &items)
{
    auto name = json[u"name"_s].toString();
    auto type = json[u"type"_s].toString();

    if (type == u"folder"_s)
    {
        // having the full folder path shouldnt be too expensive due to the shared nature of qstring
        QString folder_path_ = folder_path.isEmpty() ? name : folder_path + u" → "_s + name;

        for (const QJsonValueRef &child : json[u"children"_s].toArray())
            recursiveJsonTreeWalker(folder_path_, child.toObject(), items);
    }

    else if (type == u"url"_s)
        items.emplace_back(make_shared<BookmarkItem>(json[u"guid"_s].toString(),
                                                     name,
                                                     folder_path,
                                                     json[u"url"_s].toString()));
};

static vector<shared_ptr<BookmarkItem>> parseBookmarks(const QStringList &paths, const bool &abort)
{
    vector<shared_ptr<BookmarkItem>> results;
    for (auto &path : paths)
    {
        if (abort)
            return {};
        if (QFile f(path); f.open(QIODevice::ReadOnly))
        {
            const auto doc = QJsonDocument::fromJson(f.readAll());
            const auto roots = doc.object().value(u"roots"_s).toObject();
            for (const auto &root : roots)
                if (root.isObject())
                    recursiveJsonTreeWalker({}, root.toObject(), results);
            f.close();
        }
        else
            WARN << "Could not open Bookmarks file:" << path;
    }
    return results;
}

Plugin::Plugin()
{
    auto s = settings();
    index_hostname_ = s->value(CFG_INDEX_HOSTNAME, DEF_INDEX_HOSTNAME).toBool();

    paths_ = s->contains(CFG_BM_PATHS) ? s->value(CFG_BM_PATHS).toStringList() : defaultPaths();
    paths_.sort();

    if (!paths_.isEmpty())
        fs_watcher_.addPaths(paths_);

    connect(&fs_watcher_, &QFileSystemWatcher::fileChanged, this, [this] {
        // Update watches. Chromium seems to mv the file (inode change).
        if (!fs_watcher_.files().isEmpty())
            fs_watcher_.removePaths(fs_watcher_.files());
        fs_watcher_.addPaths(paths_);
        indexer.run();
    });

    indexer.parallel = [this](const bool &abort) { return parseBookmarks(paths_, abort); };
    indexer.finish = [this](vector<shared_ptr<BookmarkItem>> &&res) {
        INFO << u"Indexed %1 bookmarks [%2 ms]"_s
                    .arg(res.size())
                    .arg(indexer.runtime.count());

        emit statusChanged(tr("%n bookmarks indexed.", nullptr, res.size()));

        bookmarks_ = ::move(res);

        updateIndexItems();
    };
    indexer.run();
}

void Plugin::setPaths(const QStringList &paths)
{
    paths_ = paths;
    paths_.sort();

    // Chromium seems to mv the file (inode change).
    if (!fs_watcher_.files().isEmpty())
        fs_watcher_.removePaths(fs_watcher_.files());
    if (!paths_.isEmpty())
        fs_watcher_.addPaths(paths_);

    settings()->setValue(CFG_BM_PATHS, paths_);

    indexer.run();
}

QStringList Plugin::defaultPaths() const
{
    QStringList paths;
    for (auto loc : {QStandardPaths::GenericDataLocation, QStandardPaths::GenericConfigLocation})
        for (const auto &path : QStandardPaths::standardLocations(loc))
            for (const auto &app_dir_name : app_dirs)
                for (QDirIterator it(QDir(path).filePath(app_dir_name),
                                     {u"Bookmarks"_s},
                                     QDir::Files,
                                     QDirIterator::Subdirectories);
                     it.hasNext();)
                    paths << it.next();
    return paths;
}

void Plugin::resetPaths() { setPaths(defaultPaths()); }

void Plugin::updateIndexItems()
{
    vector<IndexItem> index_items;
    for (const auto &bookmark : bookmarks_)
    {
        index_items.emplace_back(static_pointer_cast<Item>(bookmark), bookmark->name_);
        if (index_hostname_)
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

    auto *string_list_model = new QStringListModel(paths_);
    connect(w, &QWidget::destroyed, string_list_model, &QObject::deleteLater);
    ui.listView_paths->setModel(string_list_model);

    ui.checkBox_index_hostname->setChecked(index_hostname_);
    connect(ui.checkBox_index_hostname, &QCheckBox::toggled, this, [this](bool checked) {
        settings()->setValue(CFG_INDEX_HOSTNAME, checked);
        index_hostname_ = checked;
        indexer.run();
    });

    ui.label_status->setText(tr("%n bookmarks indexed.", nullptr, bookmarks_.size()));
    connect(this, &Plugin::statusChanged, ui.label_status, &QLabel::setText);

    connect(ui.pushButton_add, &QPushButton::clicked, this, [this, w, m = string_list_model] {
        auto path = QFileDialog::getOpenFileName(w,
                                                 tr("Select bookmarks file"),
                                                 QDir::homePath(),
                                                 u"%1 (Bookmarks)"_s.arg(tr("Bookmarks")));

        if (!path.isNull() && !paths_.contains(path))
        {
            paths_ << path;
            setPaths(paths_);
            m->setStringList(paths_);
        }
    });

    connect(ui.pushButton_rem,
            &QPushButton::clicked,
            this,
            [this, v = ui.listView_paths, m = string_list_model]() {
                if (!v->currentIndex().isValid())
                    return;
                paths_.removeAt(v->currentIndex().row());
                setPaths(paths_);
                m->setStringList(paths_);
            });

    connect(ui.pushButton_reset, &QPushButton::clicked, this, [this, m = string_list_model]() {
        resetPaths();
        m->setStringList(paths_);
    });

    return w;
}
