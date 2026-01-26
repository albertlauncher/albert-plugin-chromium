// Copyright (c) 2022-2026 Manuel Schneider

#include "bookmarkitem.h"
#include "favicons.h"
#include <QCoreApplication>
#include <QPainter>
#include <albert/icon.h>
#include <albert/systemutil.h>
using namespace Qt::Literals;
using namespace albert;
using namespace std;

Favicons *BookmarkItem::favicons = nullptr;

struct BookmarkIcon : public Icon
{
    unique_ptr<Icon> icon_;

    BookmarkIcon(unique_ptr<Icon> e) : icon_(::move(e)) { }

    void paint(QPainter *p, const QRect &rect) override
    {
        const auto size = icon_->actualSize(rect.size(), p->device()->devicePixelRatio());
        const auto src_extent = max(size.width(), size.height());
        const auto dst_extent = min(rect.width(), rect.height());

        if (src_extent > dst_extent/2)
            Icon::iconified(icon_->clone())->paint(p, rect);
        else
            Icon::composed(Icon::image(u":star"_s),
                           Icon::iconified(icon_->clone()),
                           1.0, .5)->paint(p, rect);
    }

    bool isNull() override { return icon_->isNull(); }

    unique_ptr<Icon> clone() const override { return make_unique<BookmarkIcon>(icon_->clone()); }

    QString toUrl() const override { return u"chromium:"_s + icon_->toUrl(); }
};

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

unique_ptr<Icon> BookmarkItem::icon() const
{
    if (favicons)
        if (auto fi = favicons->iconForUrl(url_); fi)
            return make_unique<BookmarkIcon>(::move(fi));
    return Icon::image(u":star"_s);
}

vector<Action> BookmarkItem::actions() const
{
    static const auto tr_open = QCoreApplication::translate("BookmarkItem", "Open URL");
    static const auto tr_copy = QCoreApplication::translate("BookmarkItem", "Copy URL to clipboard");
    return {{u"open-url"_s, tr_open, [this]() { openUrl(url_); }},
            {u"copy-url"_s, tr_copy, [this]() { setClipboardText(url_); }}};
}
