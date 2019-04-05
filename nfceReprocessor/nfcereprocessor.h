#ifndef NFCEREPROCESSOR_H
#define NFCEREPROCESSOR_H

#include <QDomDocument>
#include <QObject>
#include <QSqlQuery>

#include "db/databasemanager.h"

class NfceReprocessor : public QObject
{
public:
    explicit NfceReprocessor(QObject *parent = nullptr);

    void process(uint transactionId, uint nnf, uint seqCode, bool useLocalNumber);

private:
    QDomDocument generateXml(uint transactionId, uint nnf, uint seqCode);
    void contingency(uint transactionId, QDomDocument xml, uint nnf, uint seqCode);
    QDomElement createElement(QString name, QString text, QDomElement &parentElement);
    QString getId(QString serie, QString nNf, uint transactionId, QString cNF, bool isCont = false);
    QString calcDV(QString id);
    QString ufId(QString uf);
    QDateTime getTransactionSendingDate(qlonglong transactionId);
    QString tBand(QSqlRecord itemFin);
    QString deviceInfo(QString value);
    QString enterpriseInfo(QString value);
    QString tPag(QSqlRecord itemFin);
    const QString removeAccents(QString s);

    uint m_nnf;
    uint m_seqCode;
    QString m_diacriticLetters;
    QStringList m_noDiacriticLetters;
};

#endif // NFCEREPROCESSOR_H
