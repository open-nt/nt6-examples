// MIT License - see LICENSE
//
// fmplay: plays or renders FM sound effects with Nt FM.
//
//   fmplay laser                     generate a laser and play it
//   fmplay coin.json                 play a patch file
//   fmplay explosion -o boom.wav     render to a WAV file instead
//   fmplay pickup --chip opl2 --seed 7 --save pickup.json
//   fmplay --demo                    a short tour: effects, a jingle, an engine loop and the OPL2
//   fmplay --bench 32                render 32 voices as fast as possible and report the CPU load
//   fmplay level1.vgz                play OPL2 or OPL3 music: VGM, VGZ, DRO or IMF
//   fmplay level1.vgz -o level1.wav  render music to a WAV file
//   fmplay piano.sbi                 play an AdLib instrument on the emulated OPL2
//   fmplay song.mid                  play a MIDI file with the General MIDI FM instruments
//   fmplay song.mid -o song.wav      render a MIDI file to a WAV file
//   fmplay song.mid --chip opl3      play a MIDI file on the emulated OPL3, as a Sound Blaster would
#include <QtCore>
#include <QtFM>

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

using namespace QtFM;

namespace {

const char *laserJson = R"({
  "format": "qtfm-patch", "version": 1, "name": "laser-small", "category": "laser",
  "algorithm": 5, "feedback": 3,
  "operators": [
    { "ratio": 1.0, "level": 1.0, "env": { "a": 0, "d": 0.12, "s": 0, "r": 0.05 } },
    { "ratio": 3.5, "level": 0.6, "env": { "a": 0, "d": 0.08, "s": 0, "r": 0.02 } },
    { "ratio": 1.0, "level": 0.8, "env": { "a": 0, "d": 0.15, "s": 0, "r": 0.05 } },
    { "ratio": 7.0, "level": 0.3, "env": { "a": 0, "d": 0.05, "s": 0, "r": 0.02 } }
  ],
  "pitch": { "base": "A5", "env": { "start": 12, "end": -24, "time": 0.18, "curve": -2 } },
  "filter": { "type": "lowpass", "cutoff": 8000, "resonance": 0.2 },
  "random": { "pitch.base": 2, "operators.1.ratio": 0.25 },
  "gain": -6
})";

// An output with no device: a thread that pulls blocks as fast as it can, to measure the engine.
class BenchmarkOutput : public AudioOutput {
public:
    bool open(int sampleRate, int channels, SampleFormat format) override {
        Q_UNUSED(format);
        m_rate = sampleRate;
        m_buffer.assign(static_cast<std::size_t>(256 * channels), 0.0f);
        m_running = true;
        m_thread = std::thread([this] {
            while (m_running.load())
                m_frames += pull(m_buffer.data(), 256);
        });
        return true;
    }
    void close() override {
        m_running = false;
        if (m_thread.joinable())
            m_thread.join();
    }
    qint64 frames() const { return m_frames.load(); }
    int rate() const { return m_rate; }

private:
    std::vector<float> m_buffer;
    std::atomic<bool> m_running{false};
    std::atomic<qint64> m_frames{0};
    std::thread m_thread;
    int m_rate = 0;
};

const struct {
    const char *name;
    Category category;
} categories[] = {
    {"pickup", Category::Pickup},       {"coin", Category::Pickup},     {"laser", Category::Laser},
    {"explosion", Category::Explosion}, {"powerup", Category::PowerUp}, {"hit", Category::Hit},
    {"jump", Category::Jump},           {"blip", Category::Blip},       {"alarm", Category::Alarm},
    {"engine", Category::Engine},
};

bool categoryNamed(const QString &name, Category *category) {
    for (const auto &c : categories) {
        if (name == QLatin1String(c.name)) {
            *category = c.category;
            return true;
        }
    }
    return false;
}

ChipType chipNamed(const QString &name) {
    const QString n = name.toLower();
    if (n == QLatin1String("opl2"))
        return ChipType::OPL2;
    if (n == QLatin1String("opl3"))
        return ChipType::OPL3;
    if (n == QLatin1String("opll"))
        return ChipType::OPLL;
    if (n == QLatin1String("opn2"))
        return ChipType::OPN2;
    if (n == QLatin1String("opm"))
        return ChipType::OPM;
    return ChipType::None;
}

// Plays patch on a started engine and returns when it has finished.
int playAndWait(QCoreApplication &app, Engine &engine, const Patch &patch, const PlayOptions &options) {
    VoiceHandle voice = engine.play(patch, options);
    if (!voice.isValid())
        return 1;
    QObject::connect(&engine, &Engine::voiceFinished, &app, [&app, &voice](quint32 id) {
        if (id == voice.id())
            QTimer::singleShot(100, &app, &QCoreApplication::quit);
    });
    if (patch.isLoop() && options.duration < 0) {
        // A loop plays until stopped: give it three seconds.
        QTimer::singleShot(3000, &app, [voice]() mutable { voice.stop(); });
    }
    return app.exec();
}

int demo(QCoreApplication &app) {
    Engine engine;
    if (!engine.start()) {
        std::fprintf(stderr, "fmplay: no sound output\n");
        return 1;
    }
    const Patch coin = Patch::generate(Category::Pickup, 3);
    engine.play(coin);

    const Patch blip = Patch::generate(Category::Blip, 8);

    // A jingle: four notes, each starting on its own sample.
    Sequence jingle;
    jingle.setTempo(150);
    const Patch pickup = Patch::generate(Category::Pickup, 11);
    jingle.addNote(0, pickup);
    jingle.addNote(0.5f, pickup, 4);
    jingle.addNote(1, pickup, 7);
    jingle.addNote(1.5f, pickup, 12);
    PlayOptions later;
    later.delay = 0.6f;
    engine.play(jingle, later);
    later.delay = 0.3f;
    engine.play(blip, later);

    const Patch laser = Patch::fromJson(laserJson);
    for (int i = 0; i < 3; ++i) {
        PlayOptions options;
        options.delay = 1.4f + 0.2f * float(i);
        engine.play(laser, options); // the random block makes each shot a little different
    }

    PlayOptions engineOptions;
    engineOptions.delay = 2.3f;
    VoiceHandle hum = engine.play(Patch::generate(Category::Engine, 42), engineOptions);
    auto *rev = new QTimer(&app);
    QElapsedTimer clock;
    clock.start();
    QObject::connect(rev, &QTimer::timeout, &app, [hum, &clock]() mutable {
        const float t = float(clock.elapsed()) / 1000.0f - 2.3f;
        hum.setPitch(std::clamp(t * 6.0f, 0.0f, 14.0f)); // smoothed on the audio thread
        if (t > 3.0f)
            hum.stop();
    });
    rev->start(20);

    PlayOptions boom;
    boom.delay = 5.5f;
    engine.play(Patch::generate(Category::Explosion, 5), boom);

    // The same kind of sounds on the emulated AdLib.
    PlayOptions onChip;
    onChip.delay = 6.8f;
    engine.play(Patch::generate(Category::Laser, 4, ChipType::OPL2), onChip);
    QTimer::singleShot(8500, &app, &QCoreApplication::quit);
    return app.exec();
}

int chipDemo(QCoreApplication &app) {
    EngineConfig config;
    config.chips = {ChipType::OPL2};
    Engine engine(config);
    Chip *opl = engine.chip(ChipType::OPL2);
    // An organ-like tone on channel 0, as an AdLib driver would set it up.
    const quint8 setup[][2] = {{0x20, 0x01}, {0x40, 0x10}, {0x60, 0xf0}, {0x80, 0x77},
                               {0x23, 0x01}, {0x43, 0x00}, {0x63, 0xf0}, {0x83, 0x77}};
    for (const auto &w : setup)
        opl->writeRegister(w[0], w[1]);
    // A C major arpeggio: frequency number 0x157 is C in block 4; key on, then off.
    const int fnums[] = {0x157, 0x1b0, 0x202, 0x2ae};
    for (int i = 0; i < 4; ++i) {
        const float t = 0.25f * float(i);
        opl->writeRegister(0xa0, quint8(fnums[i] & 0xff), t);
        opl->writeRegister(0xb0, quint8(0x20 | (4 << 2) | (fnums[i] >> 8)), t);
        opl->writeRegister(0xb0, quint8((4 << 2) | (fnums[i] >> 8)), t + 0.2f);
    }
    if (!engine.start())
        return 1;
    QTimer::singleShot(1500, &app, &QCoreApplication::quit);
    return app.exec();
}

// Writes stereo 16-bit samples as a WAV file.
QByteArray wav(const std::vector<qint16> &pcm, int rate) {
    QByteArray out;
    QDataStream s(&out, QIODevice::WriteOnly);
    s.setByteOrder(QDataStream::LittleEndian);
    const auto bytes = quint32(pcm.size() * 2);
    s.writeRawData("RIFF", 4);
    s << quint32(36 + bytes);
    s.writeRawData("WAVEfmt ", 8);
    s << quint32(16) << quint16(1) << quint16(2) << quint32(rate) << quint32(rate * 4) << quint16(4) << quint16(16);
    s.writeRawData("data", 4);
    s << bytes;
    for (qint16 v : pcm)
        s << v;
    return out;
}

enum class FileKind { Patch, Instrument, Midi, Music, Unknown };

// Recognises a file by its header; IMF music has none, so it goes by its extension.
FileKind kindOf(const QByteArray &data, const QString &suffix) {
    if (data.startsWith("MThd") || (data.startsWith("RIFF") && data.mid(8, 4) == "RMID"))
        return FileKind::Midi;
    if (data.startsWith("Vgm ") || data.startsWith("\x1f\x8b") || data.startsWith("DBRAWOPL"))
        return FileKind::Music;
    if (data.startsWith("SBI\x1a"))
        return FileKind::Instrument;
    if (data.trimmed().startsWith('{'))
        return FileKind::Patch;
    if (suffix == QLatin1String("imf") || suffix == QLatin1String("wlf"))
        return FileKind::Music;
    return FileKind::Unknown;
}

int midi(QCoreApplication &app, const QByteArray &data, const QString &output, int rate, ChipType chip) {
    QString error;
    const MidiFile song = MidiFile::fromData(data, &error);
    if (!song.isValid()) {
        std::fprintf(stderr, "fmplay: %s\n", qPrintable(error));
        return 1;
    }
    EngineConfig config;
    config.sampleRate = rate;
    config.maxVoices = 64; // one voice per sounding note
    if (chip == ChipType::OPL2 || chip == ChipType::OPL3)
        config.chips = {ChipType::OPL3}; // eighteen channels, like a Sound Blaster 16
    Engine engine(config);
    MidiPlayer player(&engine);
    player.setInstruments(PatchBank::generalMidi(chip)); // the native bank for ChipType::None
    player.setFile(song);
    if (Chip *opl = engine.chip(ChipType::OPL3))
        opl->setGain(6); // the chip's channels are quieter than native voices
    std::printf("MIDI format %d, %d track(s), %lld notes, %.1f s\n", song.format(), song.trackCount(),
                static_cast<long long>(song.notes().size()), song.duration());
    if (!output.isEmpty()) {
        // Offline: feed the player before each block, and pull the engine.
        std::vector<qint16> pcm;
        std::vector<qint16> block(2 * 1024);
        player.play();
        while (player.isPlaying()) {
            player.schedule(engine.frame() + 1024);
            engine.render(block.data(), 1024);
            pcm.insert(pcm.end(), block.begin(), block.end());
        }
        QFile file(output);
        return file.open(QIODevice::WriteOnly) && file.write(wav(pcm, rate)) > 0 ? 0 : 1;
    }
    if (!engine.start()) {
        std::fprintf(stderr, "fmplay: no sound output; use -o to render to a file\n");
        return 1;
    }
    QObject::connect(&player, &MidiPlayer::finished, &app, &QCoreApplication::quit);
    player.play();
    return app.exec();
}

int music(QCoreApplication &app, const QByteArray &data, const QString &output, int rate) {
    EngineConfig config;
    config.sampleRate = rate;
    Engine engine(config);
    QString error;
    if (!engine.music()->load(data, MusicFormat::Auto, &error)) {
        std::fprintf(stderr, "fmplay: %s\n", qPrintable(error));
        return 1;
    }
    engine.music()->setLoops(false);
    engine.music()->play();
    std::printf("%s music, %.1f s\n", engine.music()->chip() == ChipType::OPL3 ? "OPL3" : "OPL2",
                engine.music()->duration());
    if (!output.isEmpty()) {
        // Pull the engine until the music ends: rendering offline, as fast as the CPU allows.
        std::vector<qint16> pcm;
        std::vector<qint16> block(2 * 1024);
        const auto limit = qint64((engine.music()->duration() + 2) * rate);
        qint64 frames = 0;
        do {
            engine.render(block.data(), 1024);
            pcm.insert(pcm.end(), block.begin(), block.end());
            frames += 1024;
        } while (engine.music()->isPlaying() && frames < limit);
        QFile file(output);
        return file.open(QIODevice::WriteOnly) && file.write(wav(pcm, rate)) > 0 ? 0 : 1;
    }
    if (!engine.start()) {
        std::fprintf(stderr, "fmplay: no sound output; use -o to render to a file\n");
        return 1;
    }
    QObject::connect(&engine, &Engine::musicFinished, &app, &QCoreApplication::quit);
    return app.exec();
}

int bench(int voices) {
    EngineConfig config;
    config.maxVoices = voices;
    Engine engine(config);
    const Patch patch = Patch::generate(Category::Engine, 1);
    for (int i = 0; i < voices; ++i) {
        PlayOptions options;
        options.pitch = float(i % 12);
        engine.play(patch, options);
    }
    BenchmarkOutput output;
    engine.start(&output);
    QElapsedTimer timer;
    timer.start();
    std::this_thread::sleep_for(std::chrono::seconds(2));
    engine.stop();
    const double seconds = double(timer.nsecsElapsed()) / 1e9;
    const double audio = double(output.frames()) / output.rate();
    std::printf("%d voices: %.1f s of audio in %.2f s, %.2f%% of one core in real time\n", voices, audio, seconds,
                100.0 * seconds / audio);
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Plays or renders FM sound effects."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("sound"),
                                 QStringLiteral("A patch file, or a category: pickup, laser, explosion, powerup, "
                                                "hit, jump, blip, alarm or engine."));
    const QCommandLineOption outOption({QStringLiteral("o"), QStringLiteral("output")},
                                       QStringLiteral("Render to a WAV file instead of playing."),
                                       QStringLiteral("file"));
    const QCommandLineOption seedOption(QStringLiteral("seed"), QStringLiteral("Seed of the patch and its variance."),
                                        QStringLiteral("n"), QStringLiteral("1"));
    const QCommandLineOption rateOption(QStringLiteral("rate"), QStringLiteral("Sample rate."), QStringLiteral("hz"),
                                        QStringLiteral("48000"));
    const QCommandLineOption pitchOption(QStringLiteral("pitch"), QStringLiteral("Semitones to transpose by."),
                                         QStringLiteral("semitones"), QStringLiteral("0"));
    const QCommandLineOption chipOption(
        QStringLiteral("chip"),
        QStringLiteral("Generate within a chip's limits (opl2, opl3, opll, opn2 or opm), or play MIDI "
                       "on the OPL3 (opl2 or opl3)."),
        QStringLiteral("chip"));
    const QCommandLineOption saveOption(QStringLiteral("save"), QStringLiteral("Write the patch as JSON."),
                                        QStringLiteral("file"));
    const QCommandLineOption demoOption(QStringLiteral("demo"), QStringLiteral("Play a short tour."));
    const QCommandLineOption chipDemoOption(QStringLiteral("chip-demo"),
                                            QStringLiteral("Play an arpeggio by writing OPL2 registers."));
    const QCommandLineOption benchOption(QStringLiteral("bench"), QStringLiteral("Measure the CPU load of n voices."),
                                         QStringLiteral("n"));
    parser.addOptions({outOption, seedOption, rateOption, pitchOption, chipOption, saveOption, demoOption,
                       chipDemoOption, benchOption});
    parser.process(app);

    if (parser.isSet(benchOption))
        return bench(std::max(1, parser.value(benchOption).toInt()));
    if (parser.isSet(demoOption))
        return demo(app);
    if (parser.isSet(chipDemoOption))
        return chipDemo(app);
    if (parser.positionalArguments().size() != 1)
        parser.showHelp(1);

    const QString sound = parser.positionalArguments().constFirst();
    const quint32 seed = parser.value(seedOption).toUInt();
    Patch patch;
    Category category;
    if (categoryNamed(sound, &category)) {
        patch = Patch::generate(category, seed, chipNamed(parser.value(chipOption)));
    } else {
        QFile file(sound);
        if (!file.open(QIODevice::ReadOnly)) {
            std::fprintf(stderr, "fmplay: cannot read %s\n", qPrintable(sound));
            return 1;
        }
        QString error;
        const QByteArray data = file.readAll();
        const int rate = parser.value(rateOption).toInt();
        switch (kindOf(data, QFileInfo(sound).suffix().toLower())) {
        case FileKind::Midi:
            return midi(app, data, parser.value(outOption), rate, chipNamed(parser.value(chipOption)));
        case FileKind::Music:
            return music(app, data, parser.value(outOption), rate);
        case FileKind::Instrument:
            patch = Patch::fromChipInstrument(data, InstrumentFormat::Sbi, &error);
            break;
        case FileKind::Patch:
            patch = Patch::fromJson(data, &error);
            break;
        case FileKind::Unknown:
            std::fprintf(stderr, "fmplay: %s: not a patch, instrument, MIDI or music file\n", qPrintable(sound));
            return 1;
        }
        if (!patch.isValid()) {
            std::fprintf(stderr, "fmplay: %s: %s\n", qPrintable(sound), qPrintable(error));
            return 1;
        }
    }
    if (parser.isSet(saveOption)) {
        QFile out(parser.value(saveOption));
        if (!out.open(QIODevice::WriteOnly) || out.write(patch.toJson()) < 0)
            return 1;
    }

    PlayOptions options;
    options.seed = seed;
    options.pitch = parser.value(pitchOption).toFloat();
    const int rate = parser.value(rateOption).toInt();
    if (parser.isSet(outOption)) {
        const QByteArray wav = Engine::renderToWav(patch, rate, options);
        QFile file(parser.value(outOption));
        if (!file.open(QIODevice::WriteOnly) || file.write(wav) != wav.size())
            return 1;
        return 0;
    }

    EngineConfig config;
    config.sampleRate = rate;
    config.chips = {ChipType::OPL2}; // patches made for the OPL2 play on the emulated chip
    Engine engine(config);
    if (!engine.start()) {
        std::fprintf(stderr, "fmplay: no sound output; use -o to render to a file\n");
        return 1;
    }
    return playAndWait(app, engine, patch, options);
}
