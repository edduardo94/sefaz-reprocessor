#include "reprocessorrunner.h"
#include <db/databasemanager.h>
#include <QDebug>
#include <QPair>

ReprocessorRunner::ReprocessorRunner(QObject *parent) : QObject(parent),
    m_query((DatabaseManager::instance()->database())),
    m_nfce(new NfceReprocessor())
{
    /**
      to do this, its necessary a
      list with transactions to be reprocessed,
      and databases(vf_cad and vf_mov in /data directory,
      this path must to be located in same path of .exe)
    **/
    m_transactionsList = {       
    };
    m_transactionsAndNnfList = {        
    };
}
#include <QProcess>
#include <memory>
void ReprocessorRunner::run()
{
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(DatabaseManager::instance(), &DatabaseManager::connectionRefreshed, this, [&, connection](){
        DatabaseManager::instance()->disconnect(*connection);
        if(DatabaseManager::instance()->isConnected()){
             //TODO: do it for MFE and SAT
            if(!m_transactionsList.isEmpty()){
                for (auto pair : m_transactionsList) {
                    runNfce(pair, true);
                }
            }else {                
                for (auto pair : m_transactionsAndNnfList) {
                    runNfce(pair.first, false ,pair.second.first, pair.second.second);
                }
            }
            qDebug() << "fim da aplicação";
        }
    },Qt::DirectConnection);
    DatabaseManager::instance()->startConnection();
}

ReprocessorRunner::FiscalEmiterInterface ReprocessorRunner::fiscalType()
{
    m_query.prepare("SELECT emite_nfce, emite_sat, cuf FROM loja");
    if(m_query.exec() && m_query.first()){
        if(m_query.value("emite_nfce").toBool()){
            return FiscalEmiterInterface::Nfce;
        }else if(m_query.value("emite_sat").toBool()){
            if(m_query.value("cuf").toString() == "CE"){
                return FiscalEmiterInterface::MFe;
            } else {
                return FiscalEmiterInterface::Sat;
            }
        }
    }
    m_query.finish();
    m_query.clear();
    return FiscalEmiterInterface::Nfce;
}

void ReprocessorRunner::runNfce(uint transactionId, bool useLocalNumber, uint nnf, uint seqCode)
{
    m_nfce->process(transactionId, nnf, seqCode, useLocalNumber);
}
