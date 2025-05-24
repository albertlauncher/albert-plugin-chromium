// Copyright (c) 2022-2025 Manuel Schneider

#include "bookmarkitem.h"
#include <QCoreApplication>
#include <albert/albert.h>
using namespace Qt::Literals;
using namespace albert;
using namespace std;

BookmarkItem::BookmarkItem(const QString &i, const QString &n, const QString &f, const QString &u) :
    id_(i),
    name_(n),
    folder_(f),
    url_(u)
{}

QString BookmarkItem::id() const { return id_; }

QString BookmarkItem::text() const { return name_; }

QString BookmarkItem::subtext() const { return folder_; }

QString BookmarkItem::inputActionText() const { return name_; }

QStringList BookmarkItem::iconUrls() const
{
    return {
#if defined Q_OS_UNIX and not defined Q_OS_MAC
        u"xdg:www"_s,
        u"xdg:web-browser"_s,
        u"xdg:emblem-web"_s,
#endif
        u"qrc:star"_s};
}

vector<Action> BookmarkItem::actions() const
{
    static const auto tr_open = QCoreApplication::translate("BookmarkItem", "Open URL");
    static const auto tr_copy = QCoreApplication::translate("BookmarkItem", "Copy URL to clipboard");
    return {{u"open-url"_s, tr_open, [this]() { openUrl(url_); }},
            {u"copy-url"_s, tr_copy, [this]() { setClipboardText(url_); }}};
}
