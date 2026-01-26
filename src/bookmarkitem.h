// Copyright (c) 2022-2026 Manuel Schneider

#pragma once
#include <albert/item.h>
class Favicons;

class BookmarkItem : public albert::Item
{
public:

    BookmarkItem(const QString &i, const QString &n, const QString &f, const QString &u);

    QString id() const override;
    QString text() const override;
    QString subtext() const override;
    QString inputActionText() const override;
    std::unique_ptr<albert::Icon> icon() const override;
    std::vector<albert::Action> actions() const override;

    const QString id_;
    const QString name_;
    const QString folder_;
    const QString url_;

    static Favicons* favicons;
};
