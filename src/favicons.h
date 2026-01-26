// Copyright (c) 2026-2026 Manuel Schneider

#pragma once
#include <QSqlDatabase>
#include <albert/item.h>
#include <filesystem>

class Favicons
{
public:

    Favicons(std::filesystem::path db_path);
    ~Favicons();

    std::unique_ptr<albert::Icon> iconForUrl(const QString &url);

private:

    QSqlDatabase db;

};
