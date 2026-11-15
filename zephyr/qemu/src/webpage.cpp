// MIT License - see LICENSE
#include "webpage.h"

#include "board.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QList>

#ifdef CONFIG_NT_HTTPSERVER
#include <QtHttpServer/QHttpServer>
#include <QtNetwork/QNetworkInterface>
#include <QtNetwork/QTcpServer>
#ifndef QT_NO_SSL
#include <QtNetwork/QSslSocket>
#endif
#endif

namespace {

struct Result {
    QString name;
    bool ok;
    QString detail;
};

QList<Result> g_results;
bool g_finished = false;

#ifdef CONFIG_NT_HTTPSERVER

int g_requests = 0;
QElapsedTimer g_uptime;
long long g_servingAtMs = -1; // uptime of the board when the server began to listen

#ifdef CONFIG_BOARD_TARGET
const char kBoard[] = CONFIG_BOARD_TARGET;
#else
const char kBoard[] = "unknown";
#endif

// The primary Nt logo, assets/nt-logo.svg of the repository, unchanged: the brand guide asks for that.
const char kLogo[] =
    R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="256" height="256" role="img" aria-label="Nt logo">
<title>Nt</title>
<polygon points="128,24 218,76 218,180 128,232 38,180 38,76" fill="#13977f" stroke="#13977f" stroke-width="24" stroke-linejoin="round"/>
<path fill="#ffffff" d="M57.05 179.38V76.62H86.15L117.35 164.68H119.6V76.62H136.25V179.38H107.75L75.8 91.17H73.7V179.38ZM198.35 118.33H177.65V151.32Q177.65 160.47 179 163.47Q180.35 166.47 185.9 166.47L198.2 166.03L198.95 179.07Q188.9 181.03 183.65 181.03Q170.9 181.03 166.18 175.18Q161.45 169.32 161.45 153.12V118.33H151.85V104.38H161.45V82.62H177.65V104.38H198.35Z"/>
</svg>)SVG";

QString escaped(const QString &text) { return text.toHtmlEscaped(); }

QString duration(qint64 ms) {
    const qint64 s = ms / 1000;
    return s >= 60 ? QString("%1 min %2 s").arg(s / 60).arg(s % 60) : QString("%1 s").arg(s);
}

// A size in bytes, in the largest unit that keeps it readable.
QString size(long long bytes) {
    if (bytes < 0)
        return QString("unknown");
    const auto number = [](double value, long long unit) {
        const bool whole = static_cast<long long>(value * unit) % unit == 0;
        return QString::number(value, 'f', whole ? 0 : 2);
    };
    if (bytes >= 1024 * 1024)
        return number(static_cast<double>(bytes) / (1024 * 1024), 1024 * 1024) + " MiB";
    if (bytes >= 1024)
        return number(static_cast<double>(bytes) / 1024, 1024) + " KiB";
    return QString::number(bytes) + " B";
}

QString cppName(long standard) {
    switch (standard) {
    case 201103L:
        return "C++11";
    case 201402L:
        return "C++14";
    case 201703L:
        return "C++17";
    case 202002L:
        return "C++20";
    default:
        return QString("C++ (%1)").arg(standard);
    }
}

QString row(const QString &label, const QString &value) {
    return QString("<dt>%1</dt><dd>%2</dd>\n").arg(escaped(label), value);
}

// A row with a bar for how much of `total` is `used`.
QString usage(const QString &label, long long used, long long total, const QString &text) {
    const int percent = total > 0 ? static_cast<int>(used * 100 / total) : 0;
    return row(label, QString("<meter min=\"0\" max=\"%1\" value=\"%2\" aria-label=\"%3: %4% used\"></meter>"
                              "<span class=\"fine\">%5 (%4%)</span>")
                          .arg(total)
                          .arg(used)
                          .arg(escaped(label))
                          .arg(percent)
                          .arg(escaped(text)));
}

QString panel(const QString &title, const QString &rows) {
    return QString("<section class=\"panel\">\n<h3>%1</h3>\n<dl>\n%2</dl>\n</section>\n").arg(escaped(title), rows);
}

// What the board is made of, as the program itself finds out: the build configuration, the linker and the kernel.
QString boardSection() {
    const BoardInfo b = boardInfo();

    QString cpu;
    cpu += row("CPU", escaped(QString::fromUtf8(b.cpu)) + "<span class=\"fine\">" +
                          escaped(QString::fromUtf8(b.architecture)) + " architecture</span>");
    cpu += row("Cores", QString::number(b.cores));
    cpu += row("Clock", QString("%1 MHz").arg(b.clockHz / 1000000.0, 0, 'f', b.clockHz % 1000000 == 0 ? 0 : 2));
    cpu += row("Floating point", b.fpu ? "hardware" : "none, in software");
    cpu += row("Memory protection", b.mpu ? "MPU" : "none");
    cpu += row("Board", escaped(QString("%1, SoC %2%3")
                                    .arg(QString::fromUtf8(b.board), QString::fromUtf8(b.soc),
                                         b.emulated ? ", emulated by QEMU" : QString())));
    cpu += row("Kernel tick", QString("%1 Hz").arg(b.tickHz));
    if (g_servingAtMs >= 0)
        cpu +=
            row("Reset to serving", QString("%1 s").arg(g_servingAtMs / 1000.0, 0, 'f', 2) +
                                        "<span class=\"fine\">from the reset of the board to the first listen</span>");

    QString memory;
    memory += usage("Flash", b.flashUsed, b.flashSize, QString("%1 of %2").arg(size(b.flashUsed), size(b.flashSize)));
    memory += usage("RAM", b.ramImage, b.ramSize,
                    QString("%1 of %2, the image with its heaps").arg(size(b.ramImage), size(b.ramSize)));
    memory += row("C library heap",
                  escaped(size(b.libcHeap)) + "<span class=\"fine\">where Nt allocates, with malloc()</span>");
    memory += row("Kernel heap",
                  escaped(size(b.kernelHeap)) + "<span class=\"fine\">the stacks of the threads Nt starts</span>");
    if (b.mainStackPeak >= 0)
        memory += usage("Main stack", b.mainStackPeak, b.mainStack,
                        QString("%1 used at most of %2").arg(size(b.mainStackPeak), size(b.mainStack)));
    else
        memory += row("Main stack", escaped(size(b.mainStack)));
    memory += row("Interrupt stack", escaped(size(b.isrStack)));
    memory +=
        row("Thread stack", escaped(size(b.threadStack)) + "<span class=\"fine\">for each thread Nt starts</span>");
    memory += row("Network buffers", escaped(QString("receive %1, send %2, of %3 each; packets %4 and %5")
                                                 .arg(b.rxBuffers)
                                                 .arg(b.txBuffers)
                                                 .arg(size(b.bufferSize))
                                                 .arg(b.rxPackets)
                                                 .arg(b.txPackets)));

    QString software;
    software += row("Zephyr", escaped(QString::fromUtf8(b.zephyr)));
    software += row("Nt", escaped(QString::fromUtf8(qVersion())));
    software +=
        row("Compiler", escaped(QString("GCC %1, %2").arg(QString::fromUtf8(b.compiler), cppName(b.cppStandard))));
    software += row("C library", escaped(QString::fromUtf8(b.libc)));
#ifndef QT_NO_SSL
    software += row("TLS", escaped(QSslSocket::sslLibraryVersionString()));
#endif
    software += row("Kernel threads", QString::number(b.threads));

    QString network;
    for (const QNetworkInterface &i : QNetworkInterface::allInterfaces()) {
        QStringList parts;
        if (!i.hardwareAddress().isEmpty())
            parts << i.hardwareAddress();
        parts << QString("MTU %1").arg(i.maximumTransmissionUnit());
        for (const QNetworkAddressEntry &a : i.addressEntries())
            parts << QString("%1/%2").arg(a.ip().toString()).arg(a.prefixLength());
        network += row(i.name(), escaped(parts.join(" \u00b7 ")));
    }
    if (network.isEmpty())
        network = row("Interfaces", "none");

    return "<h2>The board</h2>\n<div class=\"panels\">\n" + panel("Processor", cpu) + panel("Memory", memory) +
           panel("Software", software) + panel("Network", network) + "</div>\n";
}

QString page() {
    int passed = 0;
    QString rows;
    for (const Result &r : g_results) {
        passed += r.ok ? 1 : 0;
        rows += QString("<tr><td>%1</td><td class=\"%2\">%3</td><td class=\"note\">%4</td></tr>\n")
                    .arg(escaped(r.name), r.ok ? "ok" : "bad", r.ok ? "passed" : "FAILED", escaped(r.detail));
    }
    if (g_results.isEmpty())
        rows = "<tr><td colspan=\"3\" class=\"note\">No check has finished yet.</td></tr>\n";
    const bool failed = passed != g_results.size();
    const QString state = !g_finished ? "running the self-checks" : failed ? "some checks failed" : "all checks passed";

    return QString(R"(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="refresh" content="%1">
<link rel="icon" type="image/svg+xml" href="/logo.svg">
<title>Nt on Zephyr</title>
<style>
:root { color-scheme: light dark; --bg: #f4f7f7; --card: #ffffff; --ink: #14272b; --muted: #567075; --line: #d5e0e1;
        --accent: #13977f; --ok: #14804a; --bad: #b3261e; }
@media (prefers-color-scheme: dark) {
  :root { --bg: #0e1a1c; --card: #152427; --ink: #e5f0f0; --muted: #93abae; --line: #26393c; --accent: #3fd9b4;
          --ok: #4cc38a; --bad: #ff8a80; }
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--ink); font: 16px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif; }
main { max-width: 760px; margin: 0 auto; padding: 40px 16px 56px; }
header { display: flex; align-items: center; gap: 18px; margin: 0 0 6px; }
header img { flex: none; width: 64px; height: 64px; }
h1 { margin: 0; font-size: 2rem; line-height: 1.2; letter-spacing: -0.02em; }
h1 span { color: var(--accent); }
.sub { margin: 0 0 28px; color: var(--muted); }

.cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(160px, 1fr)); gap: 12px; margin-bottom: 32px; }
.card { background: var(--card); border: 1px solid var(--line); border-radius: 12px; padding: 14px 16px; }
.card b { display: block; font-size: 1.15rem; word-break: break-word; }
.card small { color: var(--muted); }
h2 { font-size: 1.1rem; margin: 0 0 10px; }
.panels { align-items: start; display: grid; grid-template-columns: repeat(auto-fit, minmax(330px, 1fr)); gap: 12px; margin-bottom: 32px; }
.panel { background: var(--card); border: 1px solid var(--line); border-radius: 12px; padding: 14px 16px; }
.panel h3 { margin: 0 0 8px; font-size: 0.8rem; text-transform: uppercase; letter-spacing: 0.06em; color: var(--muted); }
dl { display: grid; grid-template-columns: max-content 1fr; gap: 6px 16px; margin: 0; font-size: 0.92rem; }
dt { color: var(--muted); }
dd { margin: 0; min-width: 0; overflow-wrap: anywhere; }
dd .fine { display: block; color: var(--muted); font-size: 0.82rem; }
meter { display: block; width: 100%; height: 8px; margin: 5px 0 2px; accent-color: var(--accent); }
table { width: 100%%; border-collapse: collapse; background: var(--card); border: 1px solid var(--line);
        border-radius: 12px; overflow: hidden; }
td { padding: 10px 14px; border-top: 1px solid var(--line); }
tr:first-child td { border-top: 0; }
td.ok { color: var(--ok); font-weight: 600; }
td.bad { color: var(--bad); font-weight: 700; }
td.note { color: var(--muted); font-size: 0.9rem; }
.state { display: inline-block; margin: 0 0 12px; padding: 2px 12px; border-radius: 99px; font-size: 0.9rem;
         border: 1px solid var(--line); background: var(--card); }
footer { margin-top: 32px; color: var(--muted); font-size: 0.9rem; }
a { color: var(--accent); }
</style>
</head>
<body>
<main>
<header>
<img src="/logo.svg" alt="Nt logo" width="64" height="64">
<h1>Hello from <span>Nt</span> on Zephyr</h1>
</header>
<p class="sub">This page is served by Nt HTTP Server, from a C++ program on the Zephyr RTOS running in an emulated board.</p>
<div class="cards">
<div class="card"><small>Board</small><b>%2</b></div>
<div class="card"><small>Nt</small><b>%3</b></div>
<div class="card"><small>Up for</small><b>%4</b></div>
<div class="card"><small>Page views</small><b>%5</b></div>
</div>
%10
<h2>Self-checks</h2>
<div class="state">%6: %7 of %8 so far</div>
<table>
%9</table>
<footer>The same data as JSON: <a href="/api/status">/api/status</a>. The page refreshes every few seconds.</footer>
</main>
</body>
</html>
)")
        .arg(g_finished ? 30 : 5)
        .arg(escaped(QString::fromUtf8(kBoard)), escaped(QString::fromUtf8(qVersion())), duration(g_uptime.elapsed()))
        .arg(g_requests)
        .arg(state)
        .arg(passed)
        .arg(g_results.size())
        .arg(rows)
        .arg(boardSection());
}

QJsonObject status() {
    QJsonArray checks;
    for (const Result &r : g_results)
        checks.append(QJsonObject{{"name", r.name}, {"ok", r.ok}, {"detail", r.detail}});
    const BoardInfo b = boardInfo();
    const QJsonObject hardware{{"cpu", QString::fromUtf8(b.cpu)},
                               {"architecture", QString::fromUtf8(b.architecture)},
                               {"cores", b.cores},
                               {"clockHz", b.clockHz},
                               {"fpu", b.fpu},
                               {"mpu", b.mpu},
                               {"emulated", b.emulated},
                               {"soc", QString::fromUtf8(b.soc)},
                               {"flashBytes", b.flashSize},
                               {"flashUsedBytes", b.flashUsed},
                               {"ramBytes", b.ramSize},
                               {"ramImageBytes", b.ramImage},
                               {"libcHeapBytes", b.libcHeap},
                               {"kernelHeapBytes", b.kernelHeap},
                               {"mainStackBytes", b.mainStack},
                               {"mainStackPeakBytes", b.mainStackPeak},
                               {"isrStackBytes", b.isrStack},
                               {"threadStackBytes", b.threadStack},
                               {"threads", b.threads},
                               {"zephyr", QString::fromUtf8(b.zephyr)}};
    return QJsonObject{{"board", QString::fromUtf8(kBoard)},
                       {"hardware", hardware},
                       {"nt", QString::fromUtf8(qVersion())},
                       {"uptimeMs", g_uptime.elapsed()},
                       {"resetToServingMs", g_servingAtMs},
                       {"pageViews", g_requests},
                       {"finished", g_finished},
                       {"checks", checks}};
}

#endif // CONFIG_NT_HTTPSERVER

} // namespace

void recordResult(const QString &name, bool ok, const QString &detail) { g_results.append({name, ok, detail}); }

void recordFinished() { g_finished = true; }

bool startWebServer(quint16 port, QString *error) {
#ifdef CONFIG_NT_HTTPSERVER
    g_uptime.start();
    auto *server = new QHttpServer;
    server->route("/", [] {
        ++g_requests;
        return QHttpServerResponse("text/html; charset=utf-8", page().toUtf8());
    });
    server->route("/logo.svg", [] { return QHttpServerResponse("image/svg+xml", QByteArray(kLogo)); });
    server->route("/api/status", [] { return QHttpServerResponse(status()); });
    server->route("/hello/<arg>", [](const QString &name) { return QString("Hello, %1!\n").arg(name); });

    auto *tcp = new QTcpServer(server);
    if (!tcp->listen(QHostAddress::AnyIPv4, port) || !server->bind(tcp)) {
        *error = tcp->errorString();
        delete server;
        return false;
    }
    g_servingAtMs = uptimeMs();
    return true;
#else
    Q_UNUSED(port);
    *error = QString("this build has no HTTP server (CONFIG_NT_HTTPSERVER)");
    return false;
#endif
}
