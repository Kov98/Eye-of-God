// commands.hpp — registro central de comandos do Olho de Deus (modelo VS).
// Ferramenta nova: registre aqui e ela aparece sozinha nos menus de customizacao.
// Sem Q_OBJECT (build g++ puro, sem moc).
#pragma once

#include <QHash>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <functional>

namespace cmd {

enum class Kind {
    Button,       // so icone
    TextButton,   // icone + texto (modelo VS: "Selecionar o Item de Inicializacao")
    SplitButton,  // icone + chevron que abre menu
    Combo,        // combo embutido na barra
    Check         // checkbox + rotulo
};

struct Command {
    QString id;
    QString title;
    QString category;   // grupo na barra, secao no menu de customizacao
    QString iconPath;
    QString shortcut;
    QString tooltip;
    QString defaultBars = "main";
    Kind kind = Kind::Button;
    bool checkable = false;
    bool mono = true;   // false = mantem a cor original do PNG
    std::function<void()> invoke;
    std::function<bool()> isChecked;
    std::function<bool()> isEnabled;
    std::function<QStringList()> menu_items;   // SplitButton
    std::function<void(int)> menu_invoke;      // SplitButton
    std::function<QWidget*(QWidget*)> widget_factory;  // Combo/Check
};

class Registry {
public:
    static Registry& instance() { static Registry r; return r; }

    void add(const Command& c) {
        if (c.id.isEmpty()) return;
        const int existing = index_.value(c.id, -1);
        if (existing >= 0) items_[existing] = c;
        else { index_.insert(c.id, items_.size()); items_.push_back(c); }
    }

    const Command* get(const QString& id) const {
        const int i = index_.value(id, -1);
        return i < 0 ? nullptr : &items_[i];
    }

    QVector<Command> all() const { return items_; }

    QStringList categories() const {
        QStringList out;
        for (const Command& c : items_) if (!out.contains(c.category)) out.push_back(c.category);
        return out;
    }

    QStringList defaultOrderFor(const QString& bar) const {
        QStringList out;
        for (const Command& c : items_) {
            const QStringList bars = c.defaultBars.split(',', Qt::SkipEmptyParts);
            for (const QString& b : bars) if (b.trimmed() == bar) { out.push_back(c.id); break; }
        }
        return out;
    }

private:
    QVector<Command> items_;
    QHash<QString, int> index_;
};

}  // namespace cmd
