// Copyright (c) 2026-2026 Manuel Schneider

#include "favicons.h"
#include "logging.h"
#include <QImage>
#include <QPainter>
#include <QSqlError>
#include <QSqlQuery>
#include <albert/icon.h>
#include <albert/systemutil.h>
using namespace Qt::Literals;
using namespace albert;
using namespace std;

static const auto &connection_name = "chrome_favicons"_L1;

struct QImageIcon : public Icon
{
    QImage img_;
    QString url_; // used as cheap cache key (alternatively could use hash of image data)

    QImageIcon(QImage img, QString url) : img_(::move(img)), url_(url) { }

    QSize actualSize(const QSize &device_independent_size, double device_pixel_ratio) override
    {
        // Downscaled but never larger
        if (const auto dds = device_independent_size * device_pixel_ratio;
            dds.width() < img_.width() || dds.height() < img_.height())
            return img_.size().scaled(dds, Qt::KeepAspectRatio) / device_pixel_ratio;
        else
            return img_.size() / device_pixel_ratio;
    }

    void paint(QPainter *p, const QRect &rect) override
    {
        const auto img_device_independent_size = img_.size() / p->device()->devicePixelRatio();
        const auto tgt_rect
            = QRect(rect.x() + (rect.width() - img_device_independent_size.width()) / 2,
                    rect.y() + (rect.height() - img_device_independent_size.height()) / 2,
                    img_device_independent_size.width(),
                    img_device_independent_size.height());
        p->drawImage(tgt_rect, img_);
    }

    bool isNull() override { return img_.isNull(); }

    unique_ptr<Icon> clone() const override { return make_unique<QImageIcon>(img_, url_); }

    QString toUrl() const override { return u"chrome_favicon:"_s + url_; }
};

Favicons::Favicons(filesystem::path db_path) :
    db(QSqlDatabase::addDatabase("QSQLITE"_L1, connection_name))
{
    db.setDatabaseName(toQString(db_path));
    if (!db.open())
        throw runtime_error("Failed to open Favicons database.");
}

Favicons::~Favicons()
{
    db.close();
    db = {}; // Destroy before removing connection
    QSqlDatabase::removeDatabase(connection_name);
}

unique_ptr<Icon> Favicons::iconForUrl(const QString &url)
{
    QSqlQuery query(db);

    if (!query.exec(uR"(
        SELECT fb.image_data
        FROM icon_mapping im
        JOIN favicon_bitmaps fb ON fb.icon_id = im.icon_id
        WHERE im.page_url = '%1'
        ORDER BY fb.width DESC
        LIMIT 1
    )"_s.arg(url)))
        DEBG << query.lastError();
    else if (!query.next())
        DEBG << "No favicon found for url:" << url;
    else
        return make_unique<QImageIcon>(QImage::fromData(query.value(0).toByteArray()), url);

    return {};
}
