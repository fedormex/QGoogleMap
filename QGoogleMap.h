#ifndef NAVIGINE_QT_GOOGLE_MAP_H
#define NAVIGINE_QT_GOOGLE_MAP_H

#include <QtCore/QtCore>
#include <QtGui/QtGui>
#include <QtNetwork/QtNetwork>
#include <QtXml/QtXml>

struct MapChunk
{
  QString   type        = {};
  int       zoom        = 0;
  double    latitude    = 0.0;
  double    longitude   = 0.0;
  QImage    image       = {};
};

class StdinReader: public QThread
{
    Q_OBJECT
  
  public:
    StdinReader(QObject* parent = 0);
  
  signals:
    void readLine(QString line);
    
  protected:
    void run();
  
  private:
    QTextStream mStream;
};

class QGoogleMap: public QWidget
{
    Q_OBJECT
  
  public:
    QGoogleMap(const QString& apiKey, QWidget* parent = 0);
    
    bool hasTarget()const;
    void setTarget(double latitude, double longitude, double accuracy, double azimuth);
    void setInfoText(const QString& text);
    void cancelTarget();
    
  protected:
    void keyPressEvent(QKeyEvent* event);
    void resizeEvent(QResizeEvent* event);
    void mousePressEvent(QMouseEvent* event);
    void mouseReleaseEvent(QMouseEvent* event);
    void mouseMoveEvent(QMouseEvent* event);
    void paintEvent(QPaintEvent* event);
    
  private slots:
    void refresh();
    void onZoomIn();
    void onZoomOut();
    void onScroll(int px, int py);
    void requestMap(double lat, double lon, int zoom);
    void onRequestFinished(QNetworkReply* reply);
    void onRequestTimeout(QObject* reply);
    void onReadLine(QString line);
    void onAdjustModeToggle();
    void clearCache();
    
  private:
    const QString                 mApiKey;
    QNetworkAccessManager*        mNetworkManager;
    QSignalMapper*                mNetworkTimeoutSignalMapper;
    
    QString                       mMapType;           // Map type: roadmap, ...
    int                           mMapZoom;           // Current zoom level
    double                        mDegLength;         // Number of pixels in 1 degree parallel on the current zoom level
    double                        mLatitude;          // Center latitude
    double                        mLongitude;         // Center longitude
    double                        mTargetLatitude;    // Target latitude
    double                        mTargetLongitude;   // Target longitude
    double                        mTargetAccuracy;    // Target accuracy
    double                        mTargetAzimuth;     // Target azimuth
    QList<QPair<double,double> >  mTargetHistory;
    bool                          mAdjustMode;        // Adjust mode
    QDateTime                     mAdjustTime;        // Adjust time
    QString                       mInfoText;
    
    QPoint                        mCursorPos;
    StdinReader*                  mReader;
    
    QMap<QString,MapChunk>        mMapChunks;
    
    QToolButton*                  mZoomInButton;
    QToolButton*                  mZoomOutButton;
    QToolButton*                  mAdjustButton;
};

#endif
