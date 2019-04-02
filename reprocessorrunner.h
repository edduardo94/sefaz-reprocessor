#ifndef REPROCESSORRUNNER_H
#define REPROCESSORRUNNER_H

#include <QObject>
#include <QSqlQuery>

#include <nfceReprocessor/nfcereprocessor.h>



class ReprocessorRunner : public QObject
{
    enum FiscalEmiterInterface {
        Sat = 0,
        Nfce,
        MFe
    };

    Q_OBJECT
public:
    explicit ReprocessorRunner(QObject *parent = nullptr);
    void run();
    FiscalEmiterInterface fiscalType();
    void runNfce(uint transactionId, uint nnf, uint seqCode);

private:
    QSqlQuery m_query;
    QList<QPair<uint, QPair<uint,uint>>> m_transactionsList;
//    QList<uint> m_transactionsList;
    NfceReprocessor *m_nfce;
};

#endif // REPROCESSORRUNNER_H
