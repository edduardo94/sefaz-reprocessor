#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QHash>
#include <QDir>
#include <QSqlQuery>
#include <QVariant>
#include <QtSql/QSqlDatabase>

class DatabaseManager : public QObject
{
    Q_OBJECT
public:

    static DatabaseManager * instance();

    enum DriverType {
        SQLITE
    };

    QSqlDatabase &database();
    bool isConnected();

    ~DatabaseManager();

    QDir defaultDataDir();

signals:
    void scriptingDatabase(QString databaseName);
    void scriptingError(QString databaseName, QString errorString);
    void scriptProgress(QString databaseName, int progress);
    void maxScriptSize(QString databaseName, int max);

    void connectionRefreshed();
    void tableChanged(const QString& tableName);

private slots:
    void attachCadDatabase();
public slots:
    void startConnection();
    void reconnectDB();
    void releaseConnection();
    void destroyDatabase();


private:
    static const QString DB_VERSION_ATTRIBUTE;
    static const int MOV_DB_VERSION;
    static const int CAD_DB_VERSION;

    //read-only database
    static const QString CAD_CONNECTION;
    //read-write database
    static const QString MOV_CONNECTION;

    static DatabaseManager * s_instance;

    QHash<int, QString> m_driverType;
    QSqlDatabase m_database;

    explicit DatabaseManager(QObject *parent = 0);
    QSqlDatabase openDatabase(QString databaseName, const int version);
    bool createDB(QString databaseName, QSqlDatabase database, int version);
    void upgradeDB(QString databaseName, int currentVersion, QSqlDatabase db);
    bool runSqlScript(QSqlDatabase db, QString filePath, int version);
    QHash<int, QString> registerDriversType();

};

#endif // DATABASEMANAGER_H
