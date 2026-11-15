// MIT License - see LICENSE
#pragma once

#include <QtCore/qglobal.h>
#include <QtCore/qstring.h>

// The web page of the sample: what Nt HTTP Server serves from the emulated board. The page lists the results of
// the self-checks as they come in, with a few facts about the board, and refreshes itself.

// Records the result of a check for the page.
void recordResult(const QString &name, bool ok, const QString &detail = QString());
// Tells the page that all checks are done.
void recordFinished();
// Starts serving on `port`. Returns false, with a message, if that is not possible or the build has no HTTP server.
bool startWebServer(quint16 port, QString *error);
