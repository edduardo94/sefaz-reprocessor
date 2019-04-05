#include "nfcereprocessor.h"

#include <QSqlRecord>
#include <QTime>
#include <QLocale>
#include <QMutex>
#include <QCryptographicHash>
#include <QTextStream>
#include <QUrlQuery>
#include <QSqlError>
#include <QDebug>

//this numbers(nnf and seqCode) should to be initilialized with client receipt
NfceReprocessor::NfceReprocessor(QObject *parent) : QObject(parent), m_nnf(999000001),
    m_seqCode(1)
{

}

void NfceReprocessor::process(uint transactionId, uint nnf, uint seqCode, bool useLocalNumber)
{

    uint _nnf;
    uint _seqCode;
    if(useLocalNumber){
        _nnf = m_nnf;
        _seqCode = m_seqCode;
    }else {
        _nnf = nnf;
        _seqCode = seqCode;
    }
    qDebug() << transactionId << _nnf << _seqCode;
    QDomDocument xml = generateXml(transactionId, _nnf, _seqCode);
    contingency(transactionId, xml, _nnf, _seqCode);

    m_nnf++;
    m_seqCode++;
}

void NfceReprocessor::contingency(uint transactionId, QDomDocument xml, uint nnf, uint seqCode)
{

//    QMutex mutexFolder;
//    QMutexLocker locker(&mutexFolder);
    QDomElement ide = xml.elementsByTagName("ide").at(0).toElement();
    QDateTime now = getTransactionSendingDate(transactionId);
    int offset = now.offsetFromUtc();
    now.setOffsetFromUtc(offset);
    createElement("dhCont", now.toString(Qt::ISODate), ide);
    createElement("xJust", "Problema de rede", ide);

    //alterar serie
    QDomElement serie = xml.elementsByTagName("serie").at(0).toElement();
    int vSerie = serie.childNodes().at(0).nodeValue().toInt();
    serie.childNodes().at(0).setNodeValue(QString::number(vSerie + 200));

    //atualizar id e chave
    QDomElement infNFe = xml.elementsByTagName("infNFe").at(0).toElement();
    QString nNF = QString::number(nnf).rightJustified(9, '0');
    QString id = getId(QString::number(vSerie + 200),
                       nNF,
                       transactionId,
                       QString::number(seqCode).rightJustified(8,'0'), true);
    QString dv = calcDV(id);
    QString chave = id + dv;
    infNFe.setAttribute("Id", "NFe" + chave);

    //atualiza o tipo de emissão: 9 - contigencia
    QDomElement tpEmis = xml.elementsByTagName("tpEmis").at(0).toElement();
    tpEmis.childNodes().at(0).setNodeValue("9");

    //atualiza o codigo verificador
    QDomElement cDV = xml.elementsByTagName("cDV").at(0).toElement();
    cDV.childNodes().at(0).setNodeValue(dv);

    //gerar o DigestValue
    QString infNfeStr;
    QTextStream stream(&infNfeStr);
    infNFe.save(stream,-1);

    QByteArray digestValue = QCryptographicHash::hash(infNfeStr.toLatin1(),QCryptographicHash::Sha1);
    QUrlQuery urlQueryParams;
    urlQueryParams.addQueryItem("chNFe", chave);
    urlQueryParams.addQueryItem("nVersao", "100");

    QDomElement tpAmb = xml.elementsByTagName("tbAmp").at(0).toElement();
    QString tpAmp = tpAmb.childNodes().at(0).nodeValue();
    urlQueryParams.addQueryItem("tpAmb", "1");

    QDomElement dh = xml.elementsByTagName("dhEmi").at(0).toElement();
    QString dhEmi = dh.childNodes().at(0).nodeValue();    
    urlQueryParams.addQueryItem("dhEmi", dhEmi.toLatin1().toHex());

    QDomElement nf = xml.elementsByTagName("vNF").at(0).toElement();
    QString vNF = nf.childNodes().at(0).nodeValue();
    urlQueryParams.addQueryItem("vNF", vNF);

    QDomElement icms = xml.elementsByTagName("vICMS").at(0).toElement();
    QString vICMS = icms.childNodes().at(0).nodeValue();
    urlQueryParams.addQueryItem("vICMS", vICMS);


    urlQueryParams.addQueryItem("digVal", digestValue.toHex());
    urlQueryParams.addQueryItem("cIdToken", enterpriseInfo("token_sefaz"));

    QCryptographicHash cHashQRCode(QCryptographicHash::Sha1);
    cHashQRCode.addData(QString(urlQueryParams.query() + "?" + enterpriseInfo("csc")).toUtf8());

    QString cHashQRCodeStr = cHashQRCode.result().toHex();
    urlQueryParams.addQueryItem("cHashQRCode", cHashQRCodeStr.toUpper());

    //TODO: URL_SEFAZ
    QString urlQrCode = enterpriseInfo("nfce_url_qrcode") + urlQueryParams.toString();

    QDomElement infNFeSupl = xml.createElement("infNFeSupl");

    QDomCDATASection data = xml.createCDATASection(urlQrCode);
    QDomElement qrCodeElem = xml.createElement("qrCode");
    qrCodeElem.appendChild(data);
    infNFeSupl.appendChild(qrCodeElem);

    //TODO: URL DE CONSULTA
    QDomElement urlChave = xml.createElement("urlChave");
    QDomText textDomChild = xml.createTextNode(enterpriseInfo("nfce_url_consulta"));
    urlChave.appendChild(textDomChild);
    infNFeSupl.appendChild(urlChave);

    QDomElement root = xml.elementsByTagName("NFe").at(0).toElement();
    root.appendChild(infNFeSupl);

    if(!QDir(QDir::toNativeSeparators("emissor-fiscal/sat/")).exists()){
        QDir ob;
        ob.mkpath("emissor-fiscal/nfce/");
    }

    QString path = QDir::toNativeSeparators("emissor-fiscal/nfce/NFe"+ chave +".xml");
    QFile file(QDir::toNativeSeparators(path));
    if (file.open(QIODevice::ReadWrite)){
        QTextStream stream(&file);
        stream << removeAccents(xml.toString(-1));
        file.close();
    } else {
    }
}

QString NfceReprocessor::deviceInfo(QString value)
{
    QSqlQuery query(DatabaseManager::instance()->database());
    QString prepare = "SELECT " + value +" FROM informacoes_dispositivo";
    query.prepare(prepare);
    query.exec();
    query.first();
    return query.value(value).toString();
}

QString NfceReprocessor::enterpriseInfo(QString value)
{
    QSqlQuery query(DatabaseManager::instance()->database());
    QString prepare = "SELECT " + value +" FROM loja";
    query.prepare(prepare);
    query.exec();
    query.first();
    return query.value(value).toString();
}


QDomDocument NfceReprocessor::generateXml(uint transactionId, uint nnf, uint seqCode)
{
    QDomDocument document;

    QDomElement rootNFe = document.createElement("NFe");
    rootNFe.setAttribute("xmlns", "http://www.portalfiscal.inf.br/nfe");
    document.appendChild(rootNFe);

    //Dados da Nota Fiscal eletrônica
    QDomElement infNFe = document.createElement("infNFe");
    infNFe.setAttribute("versao", "4.00");
    QString nNF = QString::number(nnf);
    QString id = getId(deviceInfo("serie_nfe").rightJustified(3, '0'),
                       nNF.rightJustified(9,'0'),
                       transactionId,
                       QString::number(seqCode).rightJustified(8, '0'));
    QString dv = calcDV(id);
    QString chave = id + dv;
    infNFe.setAttribute("Id", "NFe" + chave);
    rootNFe.appendChild(infNFe);

    //Identificação da Nota Fiscal eletrônica
    QDomElement ide = createElement("ide", "", infNFe);
    createElement("cUF", ufId(enterpriseInfo("cuf")), ide);

    createElement("cNF", QString::number(seqCode).rightJustified(8, '0'), ide);

    createElement("natOp", "VENDA DE MERCADORIA ADQUIRIDA OU RECEBIDA DE TERCEIROS", ide);
//    createElement("indPag", "0", ide); //!!!SOMENTE NA 3.10!!!
    createElement("mod", "65", ide); //nfce
    createElement("serie", deviceInfo("serie_nfe"), ide);

    createElement("nNF", nNF, ide);
    QDateTime now = getTransactionSendingDate(transactionId);
    int offset = now.offsetFromUtc();
    now.setOffsetFromUtc(offset);
    createElement("dhEmi", now.toString(Qt::ISODate), ide);
    createElement("tpNF", "1", ide); //tipo operação: saida
    createElement("idDest", "1", ide);
    createElement("cMunFG", enterpriseInfo("municipio_ibge"), ide);
    createElement("tpImp", "5", ide);
    createElement("tpEmis", "1", ide); //9: contingencia
    createElement("cDV", dv, ide);

#ifdef DEVEL
    createElement("tpAmb", "2", ide); //ambiente homologação
#else
    createElement("tpAmb", "1", ide); //ambiente produção
#endif

    createElement("finNFe", "1", ide);
    createElement("indFinal", "1", ide);
    createElement("indPres", "1", ide);
    createElement("procEmi", "0", ide);

    createElement("verProc", "1", ide);//versão

    //Identificação do Emitente da Nota Fiscal eletrônica
    QDomElement emitter = createElement("emit","", infNFe);
    createElement("CNPJ", enterpriseInfo("cnpj").rightJustified(14, '0'), emitter);
    createElement("xNome", enterpriseInfo("razao_social"), emitter);

    QDomElement enderEmit = createElement("enderEmit", "", emitter);
    createElement("xLgr", enterpriseInfo("logradouro"), enderEmit);
    createElement("nro", enterpriseInfo("numero"), enderEmit);
    QString xCpl = enterpriseInfo("complemento");
    if (!xCpl.isEmpty()) {
        createElement("xCpl", enterpriseInfo("complemento"), enderEmit);
    }
    createElement("xBairro", enterpriseInfo("bairro"), enderEmit);
    createElement("cMun", enterpriseInfo("municipio_ibge"), enderEmit);
    createElement("xMun", enterpriseInfo("cidade"), enderEmit);
    createElement("UF", enterpriseInfo("cuf"), enderEmit);
    createElement("CEP", enterpriseInfo("cep").rightJustified(8, '0'), enderEmit);

    createElement("IE", enterpriseInfo("inscricao_estadual"), emitter);
    QString ie = enterpriseInfo("inscricao_municipal");
    if(!ie.isEmpty()) {
        createElement("IM", ie, emitter);
    }
    createElement("CRT", "1", emitter);//simples nacional

    //Identificação do Cliente
    QSqlQuery vendaQuery(DatabaseManager::instance()->database());
    vendaQuery.prepare( "SELECT * FROM venda WHERE id_transacao = :transactionId");
    vendaQuery.bindValue(":transactionId", transactionId);
    vendaQuery.exec();

    if (vendaQuery.first()) {
        QString clientDoc = vendaQuery.value("cpf_cliente").toString();
        if(!clientDoc.isEmpty()){
            QDomElement dest = createElement("dest", "", infNFe);
            if (clientDoc.size() == 11) {
                createElement("CPF", clientDoc , dest);
            } else if (clientDoc.size() == 14) {
                createElement("CNPJ", clientDoc , dest);
            }
#ifdef DEVEL
            createElement("xNome","NF-E EMITIDA EM AMBIENTE DE HOMOLOGACAO - SEM VALOR FISCAL", dest);
#else
            createElement("xNome", enterpriseInfo("razao_social"), dest);
#endif

            QDomElement enderDest = createElement("enderDest", "", dest);
            createElement("xLgr", enterpriseInfo("logradouro"), enderDest);
            createElement("nro", enterpriseInfo("numero"), enderDest);
            createElement("xBairro", enterpriseInfo("bairro"), enderDest);
            createElement("cMun", enterpriseInfo("municipio_ibge"), enderDest);
            createElement("xMun", enterpriseInfo("cidade"), enderDest);
            createElement("UF", enterpriseInfo("cuf"), enderDest);
            createElement("indIEDest", "9", dest);
        }
    }

    //H. Detalhamento de Produtos e Serviços da NF-e
    QSqlQuery prodQuery(DatabaseManager::instance()->database());
    prodQuery.prepare( " SELECT * FROM itens_venda WHERE id_venda = :saleId");
    prodQuery.bindValue(":saleId", vendaQuery.value("id").toString());
    prodQuery.exec();
    prodQuery.first();

    //TODO: remover essa consulta qnd o monitor estiver pronto; isso tem que vir do temp_itens_venda
    QSqlQuery ncmProd(DatabaseManager::instance()->database());
    ncmProd.prepare( " SELECT ncm FROM produto WHERE id = :prodId");
    ncmProd.bindValue(":prodId", prodQuery.value("id_produto").toString());
    ncmProd.exec();
    ncmProd.first();

    int i = 1;
    double totalValue = 0;
    double tribSum = 0 ;
    do {
        QDomElement det = createElement("det", "", infNFe);
        det.setAttribute("nItem", QString::number(i));

        QString cEan = prodQuery.value("ean").toString();
        QDomElement prod = createElement("prod", "", det);
        createElement("cProd", prodQuery.value("codigo_produto").toString(), prod);
        createElement("cEAN",  cEan.isEmpty() ? "SEM GTIN" : cEan, prod);
        createElement("xProd", prodQuery.value("descricao").toString().trimmed(), prod);

        //TODO: isso aqui tem que vir do temp_itens_venda; alterar qnd monitor tiver pronto
        QString ncm = ncmProd.value("ncm").toString().leftJustified(8, '0');

        if (ncm.isEmpty()) {
            ncm = "00";
        }
        createElement("NCM", ncm, prod);
        int csosn = prodQuery.value("csosn_icms").toInt();
        QString icmsGroupName = "";
        switch (csosn) {
        case 102:
        case 300:
            createElement("CFOP", enterpriseInfo("cfop_cupom"), prod);
            icmsGroupName = "ICMSSN102";
            break;
        case 500:
            createElement("CFOP", enterpriseInfo("cfop_st"), prod);
            icmsGroupName = "ICMSSN500";
            break;
        default:
            break;
        }

        createElement("uCom", prodQuery.value("unidade_medida_fiscal").toString(), prod);
        createElement("qCom", QString::number(prodQuery.value("quantidade").toDouble(), 'f', 4), prod);
        createElement("vUnCom", QString::number(prodQuery.value("preco_unitario").toDouble(), 'f', 2), prod);
        createElement("vProd", QString::number(prodQuery.value("preco_unitario").toDouble() * prodQuery.value("quantidade").toDouble(), 'f', 2), prod);
        totalValue += prodQuery.value("preco_unitario").toDouble() * prodQuery.value("quantidade").toDouble();
        prodQuery.size();
        QString cEANTrib = prodQuery.value("ean").toString();

        createElement("cEANTrib", cEANTrib.isEmpty() ? "SEM GTIN" : cEANTrib, prod);

        createElement("uTrib", prodQuery.value("unidade_medida_fiscal").toString(), prod);
        createElement("qTrib", QString::number(prodQuery.value("quantidade").toDouble(), 'f', 4), prod);
        createElement("vUnTrib",  QString::number(prodQuery.value("preco_unitario").toDouble(), 'f', 2), prod);        
        if(prodQuery.value("valor_desconto").toDouble() > 0.0){
            createElement("vDesc", QString::number(prodQuery.value("valor_desconto").toDouble(), 'f', 2), prod);
        }
        createElement("indTot", "1", prod);

        QDomElement imposto = createElement("imposto", "", det);

        {
            QSqlQuery ibptQuery(DatabaseManager::instance()->database());
            ibptQuery.prepare(" SELECT "
                              "    nacional, importado, estadual, municipal "
                              " FROM "
                              "    ibpt "
                              " WHERE id = :id");
            ibptQuery.bindValue(":id", ncm);
            if (ibptQuery.exec() && ibptQuery.first()) {
                double value = prodQuery.value("valor_venda").toDouble();
                auto calcV = [value](double aliq) {return value*aliq/100;};
                double sum = calcV(QString::number(ibptQuery.value("nacional").toDouble(), 'f', 2).toDouble()) +
                        calcV(QString::number(ibptQuery.value("importado").toDouble(), 'f', 2).toDouble()) +
                        calcV(QString::number(ibptQuery.value("estadual").toDouble(), 'f', 2).toDouble()) +
                        calcV(QString::number(ibptQuery.value("municipal").toDouble(), 'f', 2).toDouble());                
                createElement("vTotTrib", QString::number(sum, 'f', 2), imposto);
                tribSum += QString::number(sum, 'f', 2).toDouble();
            } else {
                createElement("vTotTrib", "0.00", imposto);
            }
        }

        if (!icmsGroupName.isEmpty()) {
            QDomElement icmsElem = createElement("ICMS", "", imposto);
            QDomElement icmsGroup = createElement(icmsGroupName, "", icmsElem);
            createElement("orig", prodQuery.value("tabelaA").toString(), icmsGroup);
            createElement("CSOSN", prodQuery.value("csosn_icms").toString(), icmsGroup);
        }

        i++;
    } while(prodQuery.next());

    QDomElement totalElem = createElement("total", "", infNFe);
    QDomElement icmsTot = createElement("ICMSTot", "", totalElem);
    createElement("vBC", "0", icmsTot);
    createElement("vICMS", "0", icmsTot);
    createElement("vICMSDeson", "0", icmsTot);
    createElement("vFCP", "0", icmsTot);
    createElement("vBCST", "0", icmsTot);
    createElement("vST", "0", icmsTot);
    createElement("vFCPST", "0", icmsTot);
    createElement("vFCPSTRet", "0", icmsTot);
    //TODO: Fazer isso virar uma consulta
//    createElement("vProd", QString::number(saleRecord.value("valor_total").toDouble(), 'f', 2), icmsTot);
    createElement("vProd", QString::number(totalValue, 'f', 2), icmsTot);
    createElement("vFrete", "0", icmsTot);
    createElement("vSeg", "0", icmsTot);
    QString desc = QString::number(vendaQuery.value("valor_desconto").toDouble(),'f',2);
    createElement("vDesc", vendaQuery.value("valor_desconto").toString().isEmpty() ? "0" : desc , icmsTot);
    createElement("vII", "0", icmsTot);
    createElement("vIPI", "0", icmsTot);
    createElement("vIPIDevol", "0", icmsTot);
    createElement("vPIS", "0", icmsTot);
    createElement("vCOFINS", "0", icmsTot);
    createElement("vOutro", "0", icmsTot);
    //TODO: Fazer isso virar uma consulta
//    createElement("vNF", QString::number(saleRecord.value("valor_total").toDouble(), 'f', 2), icmsTot);
    createElement("vNF", QString::number(totalValue - vendaQuery.value("valor_desconto").toDouble(), 'f', 2), icmsTot);
    //TODO: Fazer isso virar uma consulta
    if(tribSum > 0)
        createElement("vTotTrib", QString::number(tribSum, 'f', 2), icmsTot);    

    QDomElement transpElem = createElement("transp", "", infNFe);
    createElement("modFrete", "9", transpElem);

    QDomElement pagElem = createElement("pag", "", infNFe);

    QSqlQuery query(DatabaseManager::instance()->database());
    query.prepare( " SELECT * FROM itens_finalizacao WHERE id_transacao = :transactionId");
    query.bindValue(":transactionId", transactionId);
    query.exec();
    query.first();


//!!! FORMAS DE PAGAMENTO NA 4.00 !!!
    double troco = 0;
    do {
        QSqlRecord itemFin = query.record();

        QDomElement detPagElem = createElement("detPag", "", pagElem);

        createElement("tPag", tPag(itemFin), detPagElem);
        createElement("vPag", QLocale::c().toString(itemFin.value("valor").toDouble(),'f',2),detPagElem);
        troco += itemFin.value("troco").toDouble();


        ///TODO: Adicionar campos do grupo "card" quando for do tipo Cartão TEF
        ///  (pag.224 Manual_de_Orientacao_Contribuinte_v_6.00.pdf - seção "YA. Formas de Pagamento")

        if (itemFin.value("especie").toInt() == 2
                /*|| itemFin.value("especie").toInt() == MeansPayment::CartaoTEF*/) {
            QSqlQuery auxQuery(DatabaseManager::instance()->database());
            auxQuery.prepare("SELECT cnpj FROM administradora_pos WHERE id = :id");
            auxQuery.bindValue(":id", "6b6dd6a8-2fa1-4ae0-aea5-18c0258b92f2"/*query.value("id_autorizador_pos").toString()*/);
            auxQuery.exec();
            if (auxQuery.first()) {
                QDomElement cardElem = createElement("card","", detPagElem);
                createElement("tpIntegra", "2", cardElem); //Tipo de integração: 1 - TEF; 2 - POS
                createElement("CNPJ", auxQuery.value("cnpj").toString(), cardElem);
                createElement("tBand", tBand(itemFin), cardElem);
                createElement("cAut", itemFin.value("nsu").toString(), cardElem);
            }

        }

    } while (query.next());
            createElement("vTroco", QLocale::c().toString(troco, 'f',2), pagElem);

    return document;
}

QDomElement NfceReprocessor::createElement(QString name, QString text, QDomElement &parentElement)
{
    QDomDocument document = parentElement.ownerDocument();
    QDomElement element = document.createElement(name);
    if (!text.isEmpty()) {
        QDomText textDomChild;
        textDomChild = document.createTextNode(text);
        element.appendChild(textDomChild);
    }
    parentElement.appendChild(element);
    return element;
}

QString NfceReprocessor::getId(QString serie, QString nNf, uint transactionId,QString cNF, bool isCont)
{
/*
 cUF - Código da UF do emitente do Documento Fiscal
 AAMM - Ano e Mês de emissão da NF-e
 CNPJ - CNPJ do emitente
 mod - Modelo do Documento Fiscal
 serie - Série do Documento Fiscal
 nNF - Número do Documento Fiscal
 tpEmis – forma de emissão da NF-e
 cNF - Código Numérico que compõe a Chave de Acesso
 cDV - Dígito Verificador da Chave de Acesso

*/
    QDateTime now = getTransactionSendingDate(transactionId);
    int offset = now.offsetFromUtc();
    now.setOffsetFromUtc(offset);
    QString id = "";
    id += ufId(enterpriseInfo("cuf")); //cUF - Código da UF do emitente do Documento Fiscal
    id += now.toString("yyMM"); //AAMM - Ano e Mês de emissão da NF-e
    id += enterpriseInfo("cnpj"); //CNPJ - CNPJ do emitente
    id += "65"; //mod -Modelo do Documento Fiscal
    id += serie; //serie - Série do Documento Fiscal
    id += nNf; //nNF - Número do Documento Fiscal (aleatorio)
    if (isCont) {
        id += "9";
    } else {
        id += "1";
    }
    id += cNF; //cNF - Código Numérico que compõe a Chave de Acesso (sequencial)
    return id;
}

QString NfceReprocessor::calcDV(QString id)
{
    const int pesos[] = {2,3,4,5,6,7,8,9}; //n = 8
    const int pesosN = 8;
    long sum = 0;
    for(int i = id.size() - 1; i >= 0; --i) {
        sum += (pesos[(id.size() - 1 - i) % pesosN] * id.at(i).digitValue());
    }
    long mod = sum % 11;
    int dv = 11 - mod;
    if (mod <= 1) {
        dv = 0;
    }

    return QString::number(dv);
}

QString NfceReprocessor::ufId(QString uf)
{
    if (uf == "RO") {
        return "11";
    } else if (uf == "AC") {
        return "12";
    } else if (uf == "AM") {
        return "13";
    } else if (uf == "RO") {
        return "14";
    } else if (uf == "PA") {
        return "15";
    } else if (uf == "AM") {
        return "16";
    } else if (uf == "TO") {
        return "17";
    } else if (uf == "MA") {
        return "21";
    } else if (uf == "PI") {
        return "22";
    } else if (uf == "CE") {
        return "23";
    } else if (uf == "RN") {
        return "24";
    } else if (uf == "PB") {
        return "25";
    } else if (uf == "PE") {
        return "26";
    } else if (uf == "AL") {
        return "27";
    } else if (uf == "SE") {
        return "28";
    } else if (uf == "BA") {
        return "29";
    } else if (uf == "MG") {
        return "31";
    } else if (uf == "ES") {
        return "32";
    } else if (uf == "RJ") {
        return "33";
    } else if (uf == "SP") {
        return "35";
    } else if (uf == "PR") {
        return "41";
    } else if (uf == "SC") {
        return "42";
    } else if (uf == "RS") {
        return "43";
    } else if (uf == "MS") {
        return "50";
    } else if (uf == "MT") {
        return "51";
    } else if (uf == "GO") {
        return "52";
    } else { // uf == "DF"
        return "53";
    }
}


QDateTime NfceReprocessor::getTransactionSendingDate(qlonglong transactionId) {
    QSqlQuery query(DatabaseManager::instance()->database());
    query.prepare("SELECT data_hora_fim FROM venda WHERE id_transacao = :transactionId");
    query.bindValue(":transactionId", transactionId);
    query.exec();
    query.first();
    return QDateTime::fromTime_t(query.value("data_hora_fim").toUInt());
}

QString NfceReprocessor::tBand(QSqlRecord itemFin)
{
/* Codigo sitef:
 * visa: 00001
 * master: 00002
 * american express: 0004
 * sorocred: 0015
*/
    QSqlQuery query(DatabaseManager::instance()->database());
    query.prepare(" SELECT "
                  "   codigoSitef "
                  " FROM "
                  "   bandeira "
                  " JOIN "
                  "   cartao "
                  " ON "
                  "   cartao.id_bandeira = bandeira.id "
                  " WHERE "
                  "   cartao.id = :id");
    query.bindValue(":id", itemFin.value("id_pos_cartao").toString());
    query.exec();
    if (query.first()) {
        switch (query.value("codigoSitef").toInt()) {
        case 1:
            return "01";
        case 2:
            return "02";
        case 4:
            return "03";
        case 15:
            return "04";
        default:
            return "99";
        }
    } else {
        return "99";
    }
}

QString NfceReprocessor::tPag(QSqlRecord itemFin)
{
    int kind = itemFin.value("especie").toInt();
    switch (kind) {
    case 1:
        return "01";
    case 4:
        return "02";
    case 2: {
        QString posModality = itemFin.value("pos_modalidade").toString();
        if (posModality == "DEBITO") {
            return "04";
        } else {
            return "03";
        }
    }
    case 3:
    default:
        return "99";
    }
}

const QString NfceReprocessor::removeAccents(QString s) {
    if (m_diacriticLetters.isEmpty()) {
        m_diacriticLetters = QString::fromUtf8("ŠŒŽšœžŸ¥µÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖØÙÚÛÜÝßàáâãäåæçèéêëìíîïðñòóôõöøùúûüýÿ");
        m_noDiacriticLetters << "S"<<"OE"<<"Z"<<"s"<<"oe"<<"z"<<"Y"<<"Y"<<"u"<<"A"<<"A"<<"A"<<"A"<<"A"<<"A"<<"AE"<<"C"<<"E"<<"E"<<"E"<<"E"<<"I"<<"I"<<"I"<<"I"<<"D"<<"N"<<"O"<<"O"<<"O"<<"O"<<"O"<<"O"<<"U"<<"U"<<"U"<<"U"<<"Y"<<"s"<<"a"<<"a"<<"a"<<"a"<<"a"<<"a"<<"ae"<<"c"<<"e"<<"e"<<"e"<<"e"<<"i"<<"i"<<"i"<<"i"<<"o"<<"n"<<"o"<<"o"<<"o"<<"o"<<"o"<<"o"<<"u"<<"u"<<"u"<<"u"<<"y"<<"y";
    }

    QString output = "";
    for (int i = 0; i < s.length(); i++) {
        QChar c = s[i];
        int dIndex = m_diacriticLetters.indexOf(c);
        if (dIndex < 0) {
            output.append(c);
        } else {
            QString replacement = m_noDiacriticLetters[dIndex];
            output.append(replacement);
        }
    }

    return output;
}
