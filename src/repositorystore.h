#ifndef REPOSITORYSTORE_H
#define REPOSITORYSTORE_H

#include "gitmodels.h"
#include <QList>
#include <QString>
#include <QStringList>

/**
 * Handles persistence of the repository list to a JSON config file.
 *
 * Keeps all file I/O, JSON (de)serialization, corruption handling and
 * atomic-write logic out of the UI layer. Methods never throw; status is
 * reported back to the caller for logging.
 */
class RepositoryStore
{
public:
    struct LoadResult {
        QList<GitRepository> repositories;
        QStringList messages; // human-readable status lines for the caller to log
    };

    /** Absolute path to the JSON config file (creating the config dir). */
    QString configFilePath() const;

    /**
     * Load repositories from disk. On a parse error the corrupt file is backed
     * up and an empty list is returned, with an explanation in messages.
     */
    LoadResult load() const;

    /**
     * Atomically write repositories to disk (temp file + rename). Returns true
     * on success; on failure sets *errorMessage when provided.
     */
    bool save(const QList<GitRepository>& repositories, QString* errorMessage = nullptr) const;
};

#endif // REPOSITORYSTORE_H
