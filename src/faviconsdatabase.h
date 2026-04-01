// Copyright (c) 2026-2026 Manuel Schneider

#pragma once
#include <QSqlDatabase>
#include <filesystem>

class FaviconsDatabase
{
public:

    FaviconsDatabase(std::filesystem::path db_path);
    ~FaviconsDatabase();

    std::optional<QImage> faviconForUrl(const QString &url);

    static std::unique_ptr<FaviconsDatabase> instance;  // implicit option flag

private:

    QSqlDatabase db;

};
