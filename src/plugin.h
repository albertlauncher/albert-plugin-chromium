// Copyright (c) 2022-2026 Manuel Schneider

#pragma once
#include <albert/indexqueryhandler.h>
#include <albert/backgroundexecutor.h>
#include <albert/extensionplugin.h>
#include <QFileSystemWatcher>
#include <memory>
#include <vector>
class BookmarkItem;
class Favicons;

class Plugin : public albert::ExtensionPlugin, public albert::IndexQueryHandler
{
    ALBERT_PLUGIN

public:
    Plugin();
    ~Plugin();

    void updateIndexItems() override;
    QWidget *buildConfigWidget() override;

    QString profilePath() const;
    void setProfilePath(const QString &);

    bool showFavicons() const;
    void setShowFavicons(bool);

    bool matchHostname() const;
    void setMatchHostname(bool);

private:
    void updateBookmarksFileWatch();
    void updateCachedDatabase();

    std::filesystem::path profile_path_;

    albert::BackgroundExecutor<std::vector<std::shared_ptr<BookmarkItem>>> indexer;
    std::vector<std::shared_ptr<BookmarkItem>> bookmarks_;
    QFileSystemWatcher bookmarks_watch_;
    bool match_hostname_;
    std::unique_ptr<Favicons> favicons_;
};
