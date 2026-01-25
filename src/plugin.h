// Copyright (c) 2022-2026 Manuel Schneider

#pragma once
#include <albert/indexqueryhandler.h>
#include <albert/backgroundexecutor.h>
#include <albert/extensionplugin.h>
#include <QFileSystemWatcher>
#include <memory>
#include <vector>
class BookmarkItem;

class Plugin : public albert::ExtensionPlugin, public albert::IndexQueryHandler
{
    ALBERT_PLUGIN

public:
    Plugin();
    void updateIndexItems() override;
    QWidget *buildConfigWidget() override;

    bool matchHostname() const;
    void setMatchHostname(bool);

    QString profilePath() const;
    void setProfilePath(const QString &);

private:
    void resetBookmarksFileWatch();

    std::filesystem::path profile_path_;
    albert::BackgroundExecutor<std::vector<std::shared_ptr<BookmarkItem>>> indexer;
    std::vector<std::shared_ptr<BookmarkItem>> bookmarks_;
    QFileSystemWatcher fs_watcher_;
    bool match_hostname_;
};
