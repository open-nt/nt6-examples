// MIT License - see LICENSE
//
// Nt FM on a Zephyr board: measures how fast the synthesiser renders on this CPU, then plays a sound effect of each
// category on the headphones every second, a fresh variation each time, with an engine hum that rises and falls
// underneath.

#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>
#include <QtFM/QtFM>

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace QtFM;

namespace {

// Renders a second of eight overlapping voices offline and prints how much of a second that took.
void measure() {
    EngineConfig config;
    config.seed = 1;
    Engine engine(config);
    PlayOptions loop;
    loop.duration = 2.0f;
    for (quint32 i = 0; i < 8; ++i)
        engine.play(Patch::generate(Category::Engine, i), loop);
    std::vector<qint16> block(static_cast<std::size_t>(config.blockSize) * 2);
    QElapsedTimer timer;
    timer.start();
    int frames = 0;
    while (frames < config.sampleRate)
        frames += engine.render(block.data(), config.blockSize);
    const qint64 us = timer.nsecsElapsed() / 1000;
    std::printf("Nt FM: 8 voices for 1 s rendered in %lld us (%.1f%% of one core)\n", static_cast<long long>(us),
                double(us) / 10000.0);
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    measure();

    auto *engine = new Engine(EngineConfig{}, &app);
    if (!engine->start()) {
        std::printf("Nt FM: the audio codec cannot be opened\n");
        return 1;
    }
    QObject::connect(engine, &Engine::underrun, &app, [] { std::printf("Nt FM: underrun\n"); });
    std::printf("Nt FM: playing on the codec at %d Hz\n", engine->config().sampleRate);

    PlayOptions humOptions;
    humOptions.gain = -12.0f;
    VoiceHandle hum = engine->play(Patch::generate(Category::Engine, 7), humOptions);

    static constexpr std::array<Category, 8> categories = {Category::Pickup, Category::Laser,   Category::Jump,
                                                           Category::Blip,   Category::PowerUp, Category::Hit,
                                                           Category::Alarm,  Category::Explosion};
    static constexpr std::array<const char *, 8> names = {"pickup", "laser", "jump",  "blip",
                                                          "power-up", "hit", "alarm", "explosion"};
    auto *effects = new QTimer(&app);
    quint32 count = 0;
    QObject::connect(effects, &QTimer::timeout, &app, [engine, &count] {
        const std::size_t i = count % categories.size();
        engine->play(Patch::generate(categories[i], count));
        std::printf("Nt FM: %s (%d voices)\n", names[i], engine->activeVoices());
        ++count;
    });
    effects->start(1000);

    auto *rev = new QTimer(&app);
    QElapsedTimer clock;
    clock.start();
    QObject::connect(rev, &QTimer::timeout, &app, [hum, &clock]() mutable {
        const float t = float(clock.elapsed()) / 1000.0f;
        hum.setPitch(6.0f + 6.0f * std::sin(t * 0.8f));
    });
    rev->start(20);

    return app.exec();
}
