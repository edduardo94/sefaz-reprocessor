#include "databasemanager.h"
#include <QSqlError>
#include <QTextStream>
#include <QSqlRecord>
#include <QHash>
#include <QStandardPaths>
#include "concurrentutils.h"

const QString DatabaseManager::DB_VERSION_ATTRIBUTE = "db_version";
const int DatabaseManager::MOV_DB_VERSION = 4;
const int DatabaseManager::CAD_DB_VERSION = 4;

const QString DatabaseManager::CAD_CONNECTION = "vf_cad";
const QString DatabaseManager::MOV_CONNECTION = "vf_mov";

DatabaseManager* DatabaseManager::s_instance = NULL;

//Separador de fim de query (END OF QUERY). Irá separar as queries que estarão nos scripts
const QString QUERY_SEPARATOR = "#EOQ#";

DatabaseManager::DatabaseManager(QObject *parent) :
    QObject(parent),
    m_driverType(registerDriversType())
{
}

DatabaseManager::~DatabaseManager()
{
}

void DatabaseManager::startConnection()
{

    RUN_CONCURRENT(DatabaseManager::startConnection);    
    //Chamar a openDatabase apenas para criar ou atualizar o banco CAD
    openDatabase(CAD_CONNECTION, CAD_DB_VERSION).close();    
    QSqlDatabase::removeDatabase(CAD_CONNECTION);    
    reconnectDB();
}

#include <QGuiApplication>
QDir DatabaseManager::defaultDataDir()
{
#ifdef Q_OS_ANDROID
//    QDir standardDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
//    standardDir.cdUp();
    QDir standardDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));

    QString dataPath = standardDir.absolutePath() + "/varejopdv/data";
    QDir dataDir(dataPath);
    if (!dataDir.exists()) {
        dataDir.mkpath(dataPath);
    }
    return dataDir;
#else
    QString dataPath = qApp->applicationDirPath() + "/data";
    QDir dataDir(dataPath);
    if (!dataDir.exists()) {
        dataDir.mkpath(dataPath);
    }
    return dataDir;
#endif
}

void DatabaseManager::attachCadDatabase()
{
    QString defaultPath = defaultDataDir().absolutePath();
    QString cadFilePath = defaultPath+"/"+CAD_CONNECTION + ".db3";
    if(QFile::exists(cadFilePath)) {
        QSqlQuery query(m_database);
        query.prepare("ATTACH ?1 AS ?2");
        query.bindValue(0,cadFilePath);
        query.bindValue(1,CAD_CONNECTION);
        query.exec();
    }
}

void DatabaseManager::reconnectDB()
{
    m_database = openDatabase(MOV_CONNECTION, MOV_DB_VERSION);
    QSqlQuery query(m_database);
    query.prepare("DETACH ?1");
    query.bindValue(0,CAD_CONNECTION);
    query.exec();
    attachCadDatabase();
    emit connectionRefreshed();
}

void DatabaseManager::releaseConnection()
{
    QSqlQuery query(m_database);
    query.prepare("DETACH ?1");
    query.bindValue(0,CAD_CONNECTION);
    query.exec();
    m_database.close();
    QSqlDatabase::removeDatabase(MOV_CONNECTION);
}

void DatabaseManager::destroyDatabase()
{
    releaseConnection();
    QDir dataDir = defaultDataDir();

    dataDir.remove(dataDir.absolutePath() + "/" + CAD_CONNECTION + ".db3");
    dataDir.remove(dataDir.absolutePath() + "/" + MOV_CONNECTION + ".db3");
}

DatabaseManager *DatabaseManager::instance()
{
    if (s_instance == NULL) {
        s_instance = new DatabaseManager();
    }
    return s_instance;
}

QSqlDatabase &DatabaseManager::database()
{
    return m_database;
}

bool DatabaseManager::isConnected()
{
    return QSqlDatabase::contains(MOV_CONNECTION);
}

bool DatabaseManager::createDB(QString databaseName, QSqlDatabase database, int version)
{
    bool ok = runSqlScript(database, ":/sql/create_"+databaseName+".sql", version);
    return ok;
}

QSqlDatabase DatabaseManager::openDatabase(QString databaseName, const int version)
{
    if (!QSqlDatabase::contains(databaseName)){
        QSqlDatabase::addDatabase(m_driverType.value(SQLITE),databaseName);
    }
    QSqlDatabase db = QSqlDatabase::database(databaseName);

    QDir dataDir = defaultDataDir();

    db.setDatabaseName(dataDir.absolutePath() + "/" + databaseName + ".db3");
    bool fileExists = dataDir.exists(db.databaseName());
    db.open();
    QSqlQuery query(db);
    if(!fileExists) {
        createDB(databaseName, db, version);
    } else {

        if (query.prepare("SELECT value FROM properties WHERE key = ?1")) {
            query.bindValue(0, DB_VERSION_ATTRIBUTE);
            if (query.exec() && query.first()) {
                if (query.record().value(0).toInt() < version) {
                    upgradeDB(databaseName, query.record().value(0).toInt(), db);
                }
            } else {
                upgradeDB(databaseName, 1, db);
            }
        }
    }

   return db;
}

void DatabaseManager::upgradeDB(QString databaseName, int currentVersion, QSqlDatabase db)
{
    switch (currentVersion) {
    case 1:
        runSqlScript(db, ":/sql/upgrade_"+databaseName+"_v2.sql", 2);
        runSqlScript(db, ":/sql/upgrade_"+databaseName+"_v3.sql", 3);
        runSqlScript(db, ":/sql/upgrade_"+databaseName+"_v4.sql", 4);
    case 2:
        runSqlScript(db, ":/sql/upgrade_"+databaseName+"_v3.sql", 3);
        runSqlScript(db, ":/sql/upgrade_"+databaseName+"_v4.sql", 4);
    case 3:
        runSqlScript(db, ":/sql/upgrade_"+databaseName+"_v4.sql", 4);
/**
 * Adicionar mais clásulas cases sem o break para cada versão do banco que quiser atualizar.
 * A inexistencia do break ira garantir que os patches de cada versao ira rodar.
 *
 * e.g.:
 *  case 2:
 *     runSqlScript(db, ":/upgrade_<NOME_BANCO>_v3"");
 *  case 3:
 *     runSqlScript(db, ":/upgrade_<NOME_BANCO>_v4");
 */
        break;
    default:
        break;
    }

}

bool DatabaseManager::runSqlScript(QSqlDatabase database, QString filePath, int version)
{
    bool allQueriesSuccessful = false;
    if (database.isValid() && database.isOpen()) {
        allQueriesSuccessful = true;
        const QString connectionName = database.connectionName();
        emit scriptingDatabase(connectionName);
        QFile scriptSqlFile(filePath);
        if (scriptSqlFile.open(QIODevice::ReadOnly)) {
            QStringList scriptQueries = QTextStream(&scriptSqlFile).readAll().split(QUERY_SEPARATOR);
            emit maxScriptSize(connectionName, scriptQueries.size());
            database.transaction();
            int progresss = 0;
            foreach (QString queryTxt, scriptQueries) {
                emit scriptProgress(connectionName, ++progresss);
                if (queryTxt.trimmed().isEmpty()) {
                    continue;
                }
                if (queryTxt.contains("DROP TABLE")) {
                    database.commit();
                    database.close();
                    database.open();
                    database.transaction();
                }
                if (database.exec(queryTxt.trimmed()).lastError().isValid()) {
                    QString errorMessage = "Falha na execução da consulta."
                                           "\n"  + queryTxt +
                                           "\nDetalhe do erro: " + database.lastError().text();
                    emit scriptingError(connectionName, errorMessage);
                    database.rollback();
                    database.close();
                    QFile::remove(database.databaseName());
                    qFatal(errorMessage.toLatin1(),"") ;
                } else {
                    QSqlQuery query(database);
                    query.prepare("INSERT OR REPLACE INTO properties (key, value) VALUES(:key,:value);");
                    query.bindValue(":key", DB_VERSION_ATTRIBUTE);
                    query.bindValue(":value", version);
                    query.exec();
                    query.finish();
                    allQueriesSuccessful &= true;
                }
            }
        }
        database.commit();
    }
    return allQueriesSuccessful;
}

QHash<int, QString> DatabaseManager::registerDriversType()
{
    QHash<int, QString> driversTypes;
    driversTypes.insert(SQLITE, "QSQLITE");
    return driversTypes;
}


