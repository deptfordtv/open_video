/***************************************************************************
 *   Copyright (C) 2007 by Jean-Baptiste Mardelle (jb@kdenlive.org)        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA          *
 ***************************************************************************/

#include "renderjob.h"

#include <QtDBus>
#include <QFile>
#include <QThread>
#include <QStringList>

// Can't believe I need to do this to sleep.
class SleepThread : QThread
{
public:
    void run() Q_DECL_OVERRIDE {}
    static void msleep(unsigned long msecs)
    {
        QThread::msleep(msecs);
    }
};

RenderJob::RenderJob(bool erase, bool usekuiserver, int pid, const QString &renderer, const QString &profile, const QString &rendermodule, const QString &player, const QString &scenelist, const QString &dest, const QStringList &preargs, const QStringList &args, int in, int out) :
    QObject(),
    m_scenelist(scenelist),
    m_dest(dest),
    m_progress(0),
    m_prog(renderer),
    m_player(player),
    m_jobUiserver(nullptr),
    m_kdenliveinterface(nullptr),
    m_usekuiserver(usekuiserver),
    m_logfile(dest + QStringLiteral(".txt")),
    m_erase(erase),
    m_seconds(0),
    m_frame(0),
    m_pid(pid),
    m_dualpass(false)
{
    m_renderProcess = new QProcess;
    m_renderProcess->setReadChannel(QProcess::StandardError);
    connect(m_renderProcess, &QProcess::stateChanged, this, &RenderJob::slotCheckProcess);

    // Disable VDPAU so that rendering will work even if there is a Kdenlive instance using VDPAU
    qputenv("MLT_NO_VDPAU", "1");

    m_args << scenelist;
    if (in != -1) {
        m_args << QStringLiteral("in=") + QString::number(in);
    }
    if (out != -1) {
        m_args << QStringLiteral("out=") + QString::number(out);
    }

    m_args << preargs;
    if (scenelist.startsWith(QLatin1String("consumer:"))) {
        // Use MLT's producer_consumer, safer to pass profile in an explicit way
        m_args << QStringLiteral("profile=") + profile;
    }
    m_args << QStringLiteral("-profile") << profile;
    m_args << QStringLiteral("-consumer") << rendermodule + QLatin1Char(':') + m_dest << QStringLiteral("progress=1") << args;

    m_dualpass = args.contains(QStringLiteral("pass=1"));

    // Create a log of every render process.
    if (!m_logfile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Unable to log to " << m_logfile.fileName();
    } else {
        m_logstream.setDevice(&m_logfile);
    }
}

RenderJob::~RenderJob()
{
    delete m_renderProcess;
    m_logfile.close();
}

void RenderJob::setLocale(const QString &locale)
{
    qputenv("LC_NUMERIC", locale.toUtf8().constData());
}

void RenderJob::slotAbort(const QString &url)
{
    if (m_dest == url) {
        slotAbort();
    }
}

void RenderJob::slotAbort()
{
    qWarning() << "Job aborted by user...";
    m_renderProcess->kill();

    if (m_kdenliveinterface) {
        m_dbusargs[1] = -3;
        m_dbusargs.append(QString());
        m_kdenliveinterface->callWithArgumentList(QDBus::NoBlock, QStringLiteral("setRenderingFinished"), m_dbusargs);
    }
    if (m_jobUiserver) {
        m_jobUiserver->call(QStringLiteral("terminate"), QString());
    }
    if (m_erase) {
        QFile(m_scenelist).remove();
    }
    QFile(m_dest).remove();
    m_logstream << "Job aborted by user" << endl;
    m_logstream.flush();
    m_logfile.close();
    qApp->quit();
}

void RenderJob::receivedStderr()
{
    QString result = QString::fromLocal8Bit(m_renderProcess->readAllStandardError()).simplified();
    if (!result.startsWith(QLatin1String("Current Frame"))) {
        m_errorMessage.append(result + QStringLiteral("<br>"));
    } else {
        m_logstream << "melt: " << result << endl;
        int pro = result.section(QLatin1Char(' '), -1).toInt();
        if (pro <= m_progress || pro <= 0 || pro > 100) {
            return;
        }
        m_progress = pro;
        if (m_args.contains(QStringLiteral("pass=1"))) {
            m_progress /= 2.0;
        } else if (m_args.contains(QStringLiteral("pass=2"))) {
            m_progress = 50 + m_progress / 2.0;
        }
        int frame = result.section(QLatin1Char(','), 1).section(QLatin1Char(' '), -1).toInt();
        if (m_kdenliveinterface && m_kdenliveinterface->isValid()) {
            m_dbusargs[1] = m_progress;
            m_kdenliveinterface->callWithArgumentList(QDBus::NoBlock, QStringLiteral("setRenderingProgress"), m_dbusargs);
        }
        if (m_jobUiserver) {
            m_jobUiserver->call(QStringLiteral("setPercent"), (uint) m_progress);
            int seconds = m_startTime.secsTo(QTime::currentTime());
            if (seconds == m_seconds) {
                return;
            }
            if (seconds < 0) {
                seconds += 24 * 60 * 60;
            }
            m_jobUiserver->call(QStringLiteral("setDescriptionField"), (uint) 0,
                                QString(), tr("Remaining time: ") + QTime(0, 0, 0).addSecs((int)(seconds * (100 - m_progress) / m_progress)).toString(QStringLiteral("hh:mm:ss")));
            //m_jobUiserver->call("setSpeed", (frame - m_frame) / (seconds - m_seconds));
            m_frame = frame;
            m_seconds = seconds;
        }
    }
}

void RenderJob::start()
{
    QDBusConnectionInterface *interface = QDBusConnection::sessionBus().interface();
    if (interface && m_usekuiserver) {
        if (!interface->isServiceRegistered(QStringLiteral("org.kde.JobViewServer"))) {
            qWarning() << "No org.kde.JobViewServer registered, trying to start kuiserver";
            if (QProcess::startDetached(QStringLiteral("kuiserver"))) {
                // Give it a couple of seconds to start
                QTime t;
                t.start();
                while (!interface->isServiceRegistered(QStringLiteral("org.kde.JobViewServer")) && t.elapsed() < 3000) {
                    SleepThread::msleep(100);   //Sleep 100 ms
                }
            } else {
                qWarning() << "Failed to start kuiserver";
            }
        }

        if (interface->isServiceRegistered(QStringLiteral("org.kde.JobViewServer"))) {
            QDBusInterface kuiserver(QStringLiteral("org.kde.JobViewServer"), QStringLiteral("/JobViewServer"), QStringLiteral("org.kde.JobViewServer"));
            QDBusReply<QDBusObjectPath> objectPath = kuiserver.asyncCall(QStringLiteral("requestView"), QLatin1String("kdenlive"), QLatin1String("kdenlive"), 0x0001);
            QString reply = ((QDBusObjectPath) objectPath).path();

            // Use of the KDE JobViewServer is an ugly hack, it is not reliable
            QString dbusView = QStringLiteral("org.kde.JobViewV2");
            m_jobUiserver = new QDBusInterface(QStringLiteral("org.kde.JobViewServer"), reply, dbusView);
            if (m_jobUiserver && m_jobUiserver->isValid()) {
                m_startTime = QTime::currentTime();
                if (!m_args.contains(QStringLiteral("pass=2"))) {
                    m_jobUiserver->call(QStringLiteral("setPercent"), (uint) 0);
                }

                m_jobUiserver->call(QStringLiteral("setInfoMessage"), tr("Rendering %1").arg(QFileInfo(m_dest).fileName()));
                QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.JobViewServer"), reply, dbusView, QStringLiteral("cancelRequested"), this, SLOT(slotAbort()));
            }
        }
    }
    initKdenliveDbusInterface();

    // Make sure the destination directory is writable
    QFileInfo checkDestination(QFileInfo(m_dest).absolutePath());
    if (!checkDestination.isWritable()) {
        slotIsOver(QProcess::NormalExit, false);
    }

    // Because of the logging, we connect to stderr in all cases.
    connect(m_renderProcess, &QProcess::readyReadStandardError, this, &RenderJob::receivedStderr);
    m_renderProcess->start(m_prog, m_args);
    m_logstream << "Started render process: " << m_prog << ' ' << m_args.join(QLatin1Char(' ')) << endl;
}

void RenderJob::initKdenliveDbusInterface()
{
    QString kdenliveId;
    QDBusConnection connection = QDBusConnection::sessionBus();
    QDBusConnectionInterface *ibus = connection.interface();
    kdenliveId = QStringLiteral("org.kde.kdenlive-%1").arg(m_pid);
    if (!ibus->isServiceRegistered(kdenliveId)) {
        kdenliveId.clear();
        const QStringList services = ibus->registeredServiceNames();
        for (const QString &service : services) {
            if (!service.startsWith(QLatin1String("org.kde.kdenlive"))) {
                continue;
            }
            kdenliveId = service;
            break;
        }
    }
    m_dbusargs.clear();
    if (kdenliveId.isEmpty()) {
        return;
    }
    m_kdenliveinterface = new QDBusInterface(kdenliveId,
            QStringLiteral("/kdenlive/MainWindow_1"),
            QStringLiteral("org.kde.kdenlive.rendering"),
            connection,
            this);

    if (m_kdenliveinterface) {
        m_dbusargs.append(m_dest);
        m_dbusargs.append((int) 0);
        if (!m_args.contains(QStringLiteral("pass=2"))) {
            m_kdenliveinterface->callWithArgumentList(QDBus::NoBlock, QStringLiteral("setRenderingProgress"), m_dbusargs);
        }
        connect(m_kdenliveinterface, SIGNAL(abortRenderJob(QString)),
                this, SLOT(slotAbort(QString)));
    }
}

void RenderJob::slotCheckProcess(QProcess::ProcessState state)
{
    if (state == QProcess::NotRunning) {
        slotIsOver(m_renderProcess->exitStatus());
    }
}

void RenderJob::slotIsOver(QProcess::ExitStatus status, bool isWritable)
{
    if (m_jobUiserver) {
        m_jobUiserver->call(QStringLiteral("setDescriptionField"), (uint) 1,
                            tr("Rendered file"), m_dest);
        //m_jobUiserver->call(QStringLiteral("terminate"), QString());
    }
    if (!isWritable) {
        QString error = tr("Cannot write to %1, check permissions.").arg(m_dest);
        if (m_kdenliveinterface) {
            m_dbusargs[1] = (int) - 2;
            m_dbusargs.append(error);
            m_kdenliveinterface->callWithArgumentList(QDBus::NoBlock, QStringLiteral("setRenderingFinished"), m_dbusargs);
        }
        QProcess::startDetached(QStringLiteral("kdialog"), QStringList() << QStringLiteral("--error") << error);
        m_logstream << error << endl;
        qApp->quit();
    }
    if (m_erase) {
        QFile(m_scenelist).remove();
    }
    if (status == QProcess::CrashExit || m_renderProcess->error() != QProcess::UnknownError || m_renderProcess->exitCode() != 0) {
        // rendering crashed
        if (m_kdenliveinterface) {
            m_dbusargs[1] = (int) - 2;
            m_dbusargs.append(m_errorMessage);
            m_kdenliveinterface->callWithArgumentList(QDBus::NoBlock, QStringLiteral("setRenderingFinished"), m_dbusargs);
        }
        QStringList args;
        QString error = tr("Rendering of %1 aborted, resulting video will probably be corrupted.").arg(m_dest);
        args << QStringLiteral("--error") << error;
        m_logstream << error << endl;
        QProcess::startDetached(QStringLiteral("kdialog"), args);
        qApp->quit();
    } else {
        if (!m_dualpass && m_kdenliveinterface) {
            m_dbusargs[1] = (int) - 1;
            m_dbusargs.append(QString());
            m_kdenliveinterface->callWithArgumentList(QDBus::NoBlock, QStringLiteral("setRenderingFinished"), m_dbusargs);
        }
        m_logstream << "Rendering of " << m_dest << " finished" << endl;
        if (!m_dualpass && m_player.length() > 3 && m_player.contains(QLatin1Char(' '))) {
            QStringList args = m_player.split(QLatin1Char(' '));
            QString exec = args.takeFirst();
            // Decode url
            QString url = QUrl::fromEncoded(args.takeLast().toUtf8()).toLocalFile();
            args << url;
            QProcess::startDetached(exec, args);
        }
        m_logstream.flush();
        if (m_dualpass) {
            emit renderingFinished();
            deleteLater();
        } else  {
            m_logfile.remove();
            qApp->quit();
        }
    }
}
