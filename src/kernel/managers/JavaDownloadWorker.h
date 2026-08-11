/*
 * Beacon - a cross-platform Minecraft launcher.
 *
 * Copyright (C) 2024-2026 fuqicn
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef JAVADOWNLOADWORKER_H
#define JAVADOWNLOADWORKER_H

#include <QObject>
#include <QString>

// Downloads a Java runtime into targetDir (the "java-<major>" folder).
// Runs inside a dedicated QThread; emits progress + a single finished signal.
class JavaDownloadWorker : public QObject
{
    Q_OBJECT
public:
    explicit JavaDownloadWorker(int majorVersion, const QString &targetDir,
                                QObject *parent = nullptr);

public slots:
    void run();

signals:
    void progressChanged(qreal progress, const QString &currentFile);
    void subTaskChanged(const QString &subTask);
    void totalFilesChanged(int total);
    void completedFilesChanged(int completed);
    void finished(bool ok, const QString &javaPath, int majorVersion);

private:
    int m_majorVersion;
    QString m_targetDir;
};

#endif
