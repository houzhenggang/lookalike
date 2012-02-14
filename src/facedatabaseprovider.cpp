#include "facedatabaseprovider.h"
#include <xqfacedatabase.h>
#include "gallerypeoplelistpage.h"
#include "nullthumbnailer.h"
#include <QStandardItemModel>
#include <QSparqlQuery>
#include <QSparqlResult>
#include <QSparqlConnection>
#include <QUrl>

FaceDatabaseProvider::FaceDatabaseProvider(QObject *parent) :
    QStandardItemModel(0, GalleryPeopleListPage::CeilingColumn, parent),
    m_sparqlConnection(new QSparqlConnection(QLatin1String("QTRACKER_DIRECT")))
{
    QString databaseName = QString(getenv("XDG_DATA_HOME")) + "/gallerycore/data/faces.db";
    NullThumbnailer thumbnailer;
    m_faceDatabase = new XQFaceDatabase(thumbnailer, databaseName);
    update();
}

FaceDatabaseProvider::~FaceDatabaseProvider()
{
    delete m_sparqlConnection;
    delete m_faceDatabase;
}

QStringList FaceDatabaseProvider::resolveContact(QString &contactUrn)
{
    static QSparqlQuery query("SELECT\n"
                              " tracker:coalesce(fn:concat(nco:nameGiven(?:contact), \" \",\n"
                              "                            nco:nameFamily(?:contact)),\n"
                              "                            nco:nameGiven(?:contact),\n"
                              "                            nco:nickname(?:contact),\n"
                              "                            nco:nameFamily(?:contact),\n"
                              "                            nco:fullname(?:contact)\n"
                              "                  )\n"
                              " ?photoUrl \n"
                              "WHERE {\n"
                              " ?:contact rdf:type nco:PersonContact .\n"
                              " OPTIONAL {\n"
                              "     ?:contact nco:photo ?photoUrn .\n"
                              "     ?photoUrn nie:url ?photoUrl .\n"
                              " }\n"
                              "}\n");

    query.bindValue("contact", QUrl(contactUrn));
    QSparqlResult *result = m_sparqlConnection->syncExec(query);
    if (result->hasError() || !result->first()) {
        delete result;
        return QStringList();
    } else {
        QStringList stringResult;
        stringResult << result->value(0).toString() << result->value(1).toString();
        delete result;
        return stringResult;
    }
}

void FaceDatabaseProvider::update()
{
    QAbstractItemModel *faceGroupsModel = m_faceDatabase->faceGroups(XQFaceDatabase::UnnamedGroup);
    clear();
    /* Add the results into our model */
    for (int faceGroupRow = 0; faceGroupRow < faceGroupsModel->rowCount(); faceGroupRow++) {
        XQFaceGroup faceGroup = m_faceDatabase->faceGroup(faceGroupsModel->index(faceGroupRow, 0).data().toString());
        QAbstractItemModel *facesModel = faceGroup.faces();
        for (int faceRow = 0; faceRow < facesModel->rowCount(); faceRow++) {
            QString sourceId = facesModel->index(faceRow, 0).data().toString();
            XQFaceRegion faceRegion = faceGroup.faceRegion(sourceId);
            QStringList faceIds = faceRegion.faceIds();
            if (faceIds.isEmpty()) {
                /* Unknown Id */
                faceIds << "Unknown";
                m_unknownImages << sourceId;
            }
            foreach(QString faceId, faceIds) {
                /* Check if entry exists, and increase the count; or add it */
                QList<QStandardItem *> row = findItems(faceId, Qt::MatchExactly, GalleryPeopleListPage::IdColumn);
                if (row.isEmpty()) {
                    if (faceId.startsWith("urn:")) {
                        QStringList resolvedValues = resolveContact(faceId);
                        if (!resolvedValues.isEmpty()) {
                            row << new QStandardItem(resolvedValues[0]);
                            row << new QStandardItem("1");
                            row << new QStandardItem(resolvedValues[1]);
                            row << new QStandardItem(faceId);
                            appendRow(row);
                        }
                    } else {
                        row << new QStandardItem(faceId);
                        row << new QStandardItem("1");
                        row << new QStandardItem(faceRegion.thumbnailPath());
                        row << new QStandardItem(faceId);
                        appendRow(row);
                    }
                } else {
                    QModelIndex index = indexFromItem(row[0]);
                    QStandardItem *countItem = item(index.row(), GalleryPeopleListPage::CountColumn);
                    int count = countItem->text().toInt();
                    count++;
                    countItem->setText(QString().setNum(count));
                }
                if (faceId.startsWith(("urn:"))) {
                    QSet<QString> images = m_suspectedImages.value(faceId);
                    images << sourceId;
                    m_suspectedImages.insert(faceId, images);
                }
            }
        }
    }

    QModelIndex startIndex = index(0,0);
    QModelIndex endIndex = index(rowCount()-1, GalleryPeopleListPage::CeilingColumn);
    emit dataChanged(startIndex, endIndex);
}

QSet<QString>& FaceDatabaseProvider::getImages()
{
    return m_unknownImages;
}

QSet<QString> FaceDatabaseProvider::getImages(const QString &faceId)
{
    return m_suspectedImages.value(faceId);
}
