#include "repositorystore.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDateTime>

QString RepositoryStore::configFilePath() const
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    return QDir(configDir).filePath("repositories.json");
}

RepositoryStore::LoadResult RepositoryStore::load() const
{
    LoadResult result;
    const QString configPath = configFilePath();

    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.messages.append("No existing configuration found, starting fresh");
        return result;
    }

    const QByteArray data = file.readAll();
    file.close();

    if (data.isEmpty()) {
        result.messages.append("Configuration file is empty, starting fresh");
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        result.messages.append(QString("Failed to parse configuration file: %1 at offset %2")
                                   .arg(parseError.errorString())
                                   .arg(parseError.offset));
        result.messages.append("Configuration file may be corrupted. Starting fresh.");

        const QString backupPath = configPath + ".corrupted." +
                                   QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        if (QFile::copy(configPath, backupPath)) {
            result.messages.append(QString("Corrupted file backed up to: %1").arg(backupPath));
        }
        return result;
    }

    if (!doc.isArray()) {
        result.messages.append("Configuration file format is invalid (expected array), starting fresh");
        return result;
    }

    const QJsonArray array = doc.array();
    for (const QJsonValue& value : array) {
        if (value.isObject()) {
            result.repositories.append(GitRepository::fromJson(value.toObject()));
        }
    }

    result.messages.append(QString("Loaded %1 repositories from configuration")
                               .arg(result.repositories.size()));
    return result;
}

bool RepositoryStore::save(const QList<GitRepository>& repositories, QString* errorMessage) const
{
    const auto fail = [errorMessage](const QString& reason) {
        if (errorMessage) {
            *errorMessage = reason;
        }
        return false;
    };

    QJsonArray array;
    for (const GitRepository& repo : repositories) {
        array.append(repo.toJson());
    }

    const QJsonDocument doc(array);
    const QString configPath = configFilePath();
    const QString tempPath = configPath + ".tmp";

    // Write to a temporary file first so a crash mid-write can't corrupt the
    // existing config.
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::WriteOnly)) {
        return fail("cannot create temporary file");
    }

    const QByteArray jsonData = doc.toJson();
    if (tempFile.write(jsonData) != jsonData.size()) {
        tempFile.remove();
        return fail("write error");
    }

    if (!tempFile.flush()) {
        tempFile.remove();
        return fail("flush error");
    }
    tempFile.close();

    // Atomically replace the original file with the temporary file.
    QFile oldFile(configPath);
    if (oldFile.exists()) {
        oldFile.remove();
    }

    if (!QFile::rename(tempPath, configPath)) {
        QFile::remove(tempPath);
        return fail("cannot replace file");
    }

    return true;
}
