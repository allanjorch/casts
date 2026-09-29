#pragma once

#include "library.h"

#include <QString>
#include <QUrl>
#include <optional>

std::optional<ParsedShow> parseFeed(const QByteArray &xml, const QUrl &base, QString *error);
QStringList parseOpml(const QByteArray &xml, QString *error);
qint64 parsePublished(const QString &text);
int parseDuration(const QString &text);
