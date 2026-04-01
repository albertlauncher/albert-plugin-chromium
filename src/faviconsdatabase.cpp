// Copyright (c) 2026-2026 Manuel Schneider

#include "faviconsdatabase.h"
#include "logging.h"
#include <QImage>
#include <QSqlError>
#include <QSqlQuery>
#include <albert/systemutil.h>
using namespace Qt::Literals;
using namespace albert;
using namespace std;

static const auto &connection_name = "chrome_favicons"_L1;

unique_ptr<FaviconsDatabase> FaviconsDatabase::instance{};

FaviconsDatabase::FaviconsDatabase(filesystem::path db_path) :
    db(QSqlDatabase::addDatabase("QSQLITE"_L1, connection_name))
{
    db.setDatabaseName(toQString(db_path));
    if (!db.open())
        throw runtime_error("Failed to open Favicons database.");
}

FaviconsDatabase::~FaviconsDatabase()
{
    db.close();
    db = {}; // Destroy before removing connection
    QSqlDatabase::removeDatabase(connection_name);
}

optional<QImage> FaviconsDatabase::faviconForUrl(const QString &url)
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
    else if (query.next())
        return QImage::fromData(query.value(0).toByteArray());
    // else
        // DEBG << "No favicon found for url:" << url;

    return {};
}
