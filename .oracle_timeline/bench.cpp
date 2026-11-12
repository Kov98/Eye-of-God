// language: C++, file: .oracle_timeline/bench.cpp
// Mede o custo real do carregador: sem limite (parse de tudo) e com o limite da GUI.
#include "timeline_index.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::printf("uso: bench <case-dir> [limite]\n");
        return 2;
    }
    const QString dir = QString::fromLocal8Bit(argv[1]);
    const int cap = argc > 2 ? std::atoi(argv[2]) : 0;
    QElapsedTimer timer;
    timer.start();
    const olho::ui::timeline_load load = olho::ui::load_case_timeline(dir, cap);
    const qint64 ms = timer.elapsed();
    std::printf("limite=%d arquivos=%d total=%lld carregados=%lld invalidas=%lld truncado=%d %lld ms\n",
                cap, load.files, static_cast<long long>(load.total),
                static_cast<long long>(load.events.size()),
                static_cast<long long>(load.invalid), load.truncated ? 1 : 0,
                static_cast<long long>(ms));
    std::printf("primeiro=%s ultimo=%s\n", qPrintable(load.first_stamp),
                qPrintable(load.last_stamp));
    return 0;
}
