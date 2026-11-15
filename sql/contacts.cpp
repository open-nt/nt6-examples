// MIT License - see LICENSE
//
// A small address book in an SQLite database: `contacts add <name> <email>`, `contacts list [filter]` and
// `contacts remove <name>`. The database file is contacts.db in the current directory, or the file named by
// the CONTACTS_DB environment variable.

#include <QtCore>
#include <QtSql>

#include <cstdio>

static bool openDatabase(const QString &file) {
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(file);
    if (!db.open()) {
        std::fprintf(stderr, "cannot open %s: %s\n", qPrintable(file), qPrintable(db.lastError().text()));
        return false;
    }
    QSqlQuery query;
    return query.exec("CREATE TABLE IF NOT EXISTS contacts ("
                      "id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, email TEXT)");
}

static bool addContact(const QString &name, const QString &email) {
    QSqlQuery query;
    query.prepare("INSERT INTO contacts (name, email) VALUES (:name, :email)");
    query.bindValue(":name", name);
    query.bindValue(":email", email);
    if (!query.exec()) {
        std::fprintf(stderr, "cannot add %s: %s\n", qPrintable(name), qPrintable(query.lastError().text()));
        return false;
    }
    return true;
}

static void listContacts(const QString &filter) {
    QSqlQuery query;
    query.prepare("SELECT name, email FROM contacts WHERE name LIKE ? ORDER BY name");
    query.addBindValue("%" + filter + "%");
    query.exec();
    while (query.next())
        std::printf("%-20s %s\n", qPrintable(query.value(0).toString()), qPrintable(query.value(1).toString()));
}

static bool removeContact(const QString &name) {
    QSqlTableModel model;
    model.setTable("contacts");
    model.setEditStrategy(QSqlTableModel::OnManualSubmit);
    model.select();
    const int nameColumn = model.fieldIndex("name");
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.data(model.index(row, nameColumn)).toString() == name) {
            model.removeRows(row, 1);
            return model.submitAll();
        }
    }
    return false;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    const QString command = args.value(1);
    if (!openDatabase(qEnvironmentVariable("CONTACTS_DB", "contacts.db")))
        return 1;
    if (command == "add" && args.size() == 4)
        return addContact(args.at(2), args.at(3)) ? 0 : 1;
    if (command == "list" && args.size() <= 3) {
        listContacts(args.value(2));
        return 0;
    }
    if (command == "remove" && args.size() == 3)
        return removeContact(args.at(2)) ? 0 : 1;
    std::fputs("usage: contacts add <name> <email> | list [filter] | remove <name>\n", stderr);
    return 2;
}
