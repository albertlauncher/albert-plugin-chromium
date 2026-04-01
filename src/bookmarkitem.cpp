// Copyright (c) 2022-2026 Manuel Schneider

#include "bookmarkitem.h"
#include "faviconsdatabase.h"
#include <QCoreApplication>
#include <QPainter>
#include <albert/icon.h>
#include <albert/systemutil.h>
using namespace Qt::Literals;
using namespace albert;
using namespace std;

struct FaviconIcon : public Icon
{
    QString url;
    optional<QImage> img;  // lazy loaded

    FaviconIcon(const QString &url) : url(::move(url)) { }

    void loadLazy()
    {
        if (!img)
        {
            if (FaviconsDatabase::instance)
                img = FaviconsDatabase::instance->faviconForUrl(url);
            else
                img = QImage();
        }
    }

    bool isNull() override
    {
        loadLazy();
        return img->isNull();
    }

    unique_ptr<Icon> clone() const override
    {
        auto icon = make_unique<FaviconIcon>(url);
        icon->img = img;  // share loaded image
        return icon;
    }

    QString toUrl() const override { return u"chrome_favicon:"_s + url; }

    QSize actualSize(const QSize &device_independent_size, double) override
    {
        if (img->isNull())  // calls loadLazy();
            return {};

        return device_independent_size;
    }

    void paint(QPainter *p, const QRect &rect) override
    {
        if (img->isNull())  // calls loadLazy();
            return;

        p->save();
        p->setRenderHint(QPainter::SmoothPixmapTransform, true);
        p->drawImage(rect, *img);
        p->restore();
    }
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
    if (FaviconsDatabase::instance)
        if (auto favicon = make_unique<FaviconIcon>(url_); !favicon->isNull())
            return Icon::iconified(::move(favicon));

    return Icon::image(u":star"_s);
}

vector<Action> BookmarkItem::actions() const
{
    static const auto tr_open = QCoreApplication::translate("BookmarkItem", "Open URL");
    static const auto tr_copy = QCoreApplication::translate("BookmarkItem", "Copy URL to clipboard");
    return {{u"open-url"_s, tr_open, [this]() { openUrl(url_); }},
            {u"copy-url"_s, tr_copy, [this]() { setClipboardText(url_); }}};
}
