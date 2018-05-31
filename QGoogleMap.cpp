#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>
#include <signal.h>

#include "QGoogleMap.h"

const int     MEM_CACHE_SIZE  = 200;    // In chunks
const int     DISK_CACHE_SIZE = 10000;  // In chunks
const int     HISTORY_SIZE    = 1000;   // maximum history (track) size
const int     ZOOM_MAX        = 19;     // maximum zoom value
const int     ZOOM_MIN        = 10;     // minimum zoom value
const double  EPSILON         = 1e-8;

const double DEG_LENGTH_ARRAY[] = {
    0,            // Zoom level 0
    0,            // Zoom level 1
    0,            // Zoom level 2
    0,            // Zoom level 3
    0,            // Zoom level 4
    22.8,         // Zoom level 5
    0,            // Zoom level 6
    0,            // Zoom level 7
    0,            // Zoom level 8
    0,            // Zoom level 9
    727.142857,   // Zoom level 10
    1454.285714,  // Zoom level 11
    2908.571428,  // Zoom level 12
    0,            // Zoom level 13
    0,            // Zoom level 14
    0,            // Zoom level 15
    46625.0,      // Zoom level 16
    93250.0,      // Zoom level 17
    186500.0,     // Zoom level 18
    373000.0,     // Zoom level 19
    0 };          // Zoom level 20

const QString FFMPEG = "ffmpeg";

StdinReader::StdinReader(QObject* parent)
  : QThread ( parent )
  , mStream ( stdin, QIODevice::ReadOnly )
{
  setTerminationEnabled(true);
}

void StdinReader::run()
{
  while (true)
  {
    QString line = mStream.readLine();
    if (line.isEmpty())
    {
      usleep(100000);
      continue;
    }
    
    emit readLine(line);
  }
}

CacheCleaner::CacheCleaner(const QString& cacheDir, QObject* parent)
  : QThread   ( parent )
  , mCacheDir ( cacheDir )
{
  setTerminationEnabled(true);
}

void CacheCleaner::run()
{
  while (true)
  {
    QDir dir(mCacheDir);
    QStringList fileList = dir.entryList(QStringList() << "*.png", QDir::Files, QDir::Time);
    
    if (fileList.size() > DISK_CACHE_SIZE)
    {
      while (fileList.size() > DISK_CACHE_SIZE / 2)
      {
        qDebug() << "Removing file" << fileList.last();
        dir.remove(fileList.last());
        fileList.removeLast();
      }
    }
    usleep(60000000);
  }
}

QGoogleMap::QGoogleMap(const QString& apiKey, QWidget* parent)
  : QWidget          ( parent )
  , mApiKey          ( apiKey )
  , mHomeDir         ( "/var/tmp/QGoogleMap" )
  , mMapType         ( "roadmap" )
  , mMapZoom         ( 18  )
  , mDegLength       ( DEG_LENGTH_ARRAY[mMapZoom] )
  , mLatitude        ( 42.531  )
  , mLongitude       ( -71.149 )
  , mTargetLatitude  ( 0.0 )
  , mTargetLongitude ( 0.0 )
  , mTargetAccuracy  ( 0.0 )
  , mAdjustTime      ( QDateTime::currentDateTime() )
  , mGpsTime         ( QDateTime::currentDateTime() )
  , mRecordProcess   ( 0 )
{
  mkdir(qPrintable(mHomeDir), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  mkdir(qPrintable(mHomeDir + "/cache"), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  mkdir(qPrintable(mHomeDir + "/video"), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  mkdir(qPrintable(mHomeDir + "/logs"),  S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  
  mNetworkManager = new QNetworkAccessManager(this);
  connect(mNetworkManager, SIGNAL(finished(QNetworkReply*)), this, SLOT(onRequestFinished(QNetworkReply*)));
  
  mNetworkTimeoutSignalMapper = new QSignalMapper(this);
  connect(mNetworkTimeoutSignalMapper, SIGNAL(mapped(QObject*)),
          this, SLOT(onRequestTimeout(QObject*)));
  
  QTimer* refreshTimer = new QTimer(this);
  refreshTimer->setInterval(250);
  refreshTimer->setSingleShot(false);
  refreshTimer->start();
  connect(refreshTimer, SIGNAL(timeout()), this, SLOT(refresh()));
  
  mReader = new StdinReader(this);
  connect(mReader, SIGNAL(readLine(QString)), this, SLOT(onReadLine(QString)));
  mReader->start();
  
  mCacheCleaner = new CacheCleaner(mHomeDir + "/cache", this);
  mCacheCleaner->start();
  
  mZoomInButton = new QToolButton(this);
  mZoomInButton->setStyleSheet("background-color: rgba(200,200,200,200); border-style: solid; border-radius: 30px;");
  mZoomInButton->setIcon(QIcon(":/icons/zoom_in"));
  mZoomInButton->setFixedSize(60, 60);
  mZoomInButton->setEnabled(mMapZoom < ZOOM_MAX);
  connect(mZoomInButton, SIGNAL(clicked()), this, SLOT(onZoomIn()));
  
  mZoomOutButton = new QToolButton(this);
  mZoomOutButton->setStyleSheet("background-color: rgba(200,200,200,200); border-style: solid; border-radius: 30px;");
  mZoomOutButton->setIcon(QIcon(":/icons/zoom_out"));
  mZoomOutButton->setFixedSize(60, 60);
  mZoomOutButton->setEnabled(mMapZoom > ZOOM_MIN);
  connect(mZoomOutButton, SIGNAL(clicked()), this, SLOT(onZoomOut()));
  
  mAdjustButton = new QToolButton(this);
  mAdjustButton->setStyleSheet("QToolButton { background-color: rgba(200,200,200,200); border-style: solid; border-radius: 30px; } "
                               "QToolButton:checked { background-color: rgba(150,150,150,200); border-style: solid; border-radius: 30px; } ");
  mAdjustButton->setCheckable(true);
  mAdjustButton->setChecked(false);
  mAdjustButton->setFixedSize(60, 60);
  connect(mAdjustButton, SIGNAL(toggled(bool)), this, SLOT(onAdjustModeToggle()));
  QTimer::singleShot(0, mAdjustButton, SLOT(toggle()));
  
  mRecordButton = new QToolButton(this);
  mRecordButton->setIcon(QIcon(":/icons/record_start"));
  mRecordButton->setStyleSheet("QToolButton { background-color: rgba(200,200,200,200); border-style: solid; border-radius: 30px; } "
                               "QToolButton:checked { background-color: rgba(150,150,150,200); border-style: solid; border-radius: 30px; } ");
  mRecordButton->setCheckable(true);
  mRecordButton->setChecked(false);
  mRecordButton->setFixedSize(60, 60);
  connect(mRecordButton, SIGNAL(toggled(bool)), this, SLOT(onRecordToggle()));
}

void QGoogleMap::setTarget(double latitude, double longitude, double accuracy, double azimuth)
{
  mTargetLatitude  = latitude;
  mTargetLongitude = longitude;
  mTargetAccuracy  = accuracy;
  mTargetAzimuth   = azimuth;
  
  if (hasTarget())
  {
    mTargetHistory.append(qMakePair(mTargetLatitude, mTargetLongitude));
    if (mTargetHistory.size() > HISTORY_SIZE)
      mTargetHistory.removeFirst();
  }
  
  refresh();
  update();
}

void QGoogleMap::setInfoText(const QString& text)
{
  mInfoText = text;
  update();
}

void QGoogleMap::cancelTarget()
{
  mTargetLatitude  = 0.0;
  mTargetLongitude = 0.0;
  mTargetAccuracy  = 0.0;
  mTargetHistory.clear();
}

bool QGoogleMap::hasTarget()const
{
  return (qAbs(mTargetLatitude)  > EPSILON || qAbs(mTargetLongitude) > EPSILON) &&
          qAbs(mTargetLatitude)  <= 89.0 &&
          qAbs(mTargetLongitude) <= 180.0;
}

void QGoogleMap::keyPressEvent(QKeyEvent* event)
{
  if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal)
    onZoomIn();
  else if (event->key() == Qt::Key_Minus)
    onZoomOut();
  else if (event->key() == Qt::Key_Q)
    close();
}

void QGoogleMap::resizeEvent(QResizeEvent* event)
{
  const int buttonWidth  = mZoomInButton->width();
  const int buttonHeight = mZoomInButton->height();
  const int padding      = 10;
  
  mZoomInButton  -> move(width() - buttonWidth - 10, height() / 2 - 3 * padding / 2 - 2 * buttonHeight);
  mZoomOutButton -> move(width() - buttonWidth - 10, height() / 2 - padding / 2 - buttonHeight);
  mAdjustButton  -> move(width() - buttonWidth - 10, height() / 2 + padding / 2);
  mRecordButton  -> move(width() - buttonWidth - 10, height() / 2 + 3 * padding / 2 + buttonHeight);
  update();
}

void QGoogleMap::mousePressEvent(QMouseEvent* event)
{
  if (event->buttons() == Qt::LeftButton)
  {
    mCursorPos = event->pos();
    event->accept();
  }
  else
    event->ignore();
}

void QGoogleMap::mouseReleaseEvent(QMouseEvent* event)
{
  mCursorPos = QPoint();
  event->accept();
}

void QGoogleMap::mouseMoveEvent(QMouseEvent* event)
{
  if (event->buttons() == Qt::LeftButton)
  {
    const int px = event->pos().x() - mCursorPos.x();
    const int py = event->pos().y() - mCursorPos.y();
    mCursorPos = event->pos();
    onScroll(px, py);
    event->accept();
  }
}

void QGoogleMap::paintEvent(QPaintEvent* event)
{
  const double LATITUDE_COEF        = 1.0 / cos(mLatitude * M_PI / 180);
  const double PARALLEL_DEG_LENGTH  = 40000000.0 / 360 / LATITUDE_COEF;
  
  const QFont defaultFont = this->font();
  
  QPainter p;
  p.begin(this);
  
  p.setRenderHints(QPainter::Antialiasing |
                   QPainter::TextAntialiasing |
                   QPainter::SmoothPixmapTransform |
                   QPainter::HighQualityAntialiasing |
                   QPainter::NonCosmeticDefaultPen,
                   true);
  
  // Drawing gray background
  p.fillRect(0, 0, width(), height(), QColor(Qt::gray));
  
  // Drawing map chunks
  for(auto chunk: mMapChunks)
  {
    if (chunk.zoom != mMapZoom)
      continue;
    
    double dx = (chunk.longitude - mLongitude);
    double dy = (chunk.latitude  - mLatitude);
    qint64 px = width()  / 2 + (qint64)round(dx * mDegLength) - chunk.image.width()  / 2;
    qint64 py = height() / 2 - (qint64)round(dy * mDegLength * LATITUDE_COEF) - chunk.image.height() / 2;
    
    if (px > -chunk.image.width()  && px < width() &&
        py > -chunk.image.height() && py < height())
      p.drawImage(px, py, chunk.image);
  }
  
  // Drawing target
  if (hasTarget())
  {
    // Drawing track
    if (!mTargetHistory.isEmpty())
    {
      QPainterPath path;
      for(int i = 0; i < mTargetHistory.size(); ++i)
      {
        double dx = mTargetHistory[i].second - mLongitude;
        double dy = mTargetHistory[i].first  - mLatitude;
        qint64 px = width()  / 2 + (qint64)round(dx * mDegLength);
        qint64 py = height() / 2 - (qint64)round(dy * mDegLength * LATITUDE_COEF);
        if (i == 0)
          path.moveTo(px, py);
        else
          path.lineTo(px, py);
      }
      p.setPen(QColor(255, 100, 0, 255));
      p.drawPath(path);
    }
    
    double dx = mTargetLongitude - mLongitude;
    double dy = mTargetLatitude  - mLatitude;
    qint64 px = width()  / 2 + (qint64)round(dx * mDegLength);
    qint64 py = height() / 2 - (qint64)round(dy * mDegLength * LATITUDE_COEF);
    
    int radius  = mTargetAccuracy * 10 * mDegLength / PARALLEL_DEG_LENGTH; // External radius: navigation-determined, transparent
    int radius1 = 25;                                                      // Internal radius: fixed, solid
    
    if (px >= -100 && px < width()  + 100 &&
        py >= -100 && py < height() + 100)
    {
      p.setPen   ( QColor(255, 100, 0, 0) );
      p.setBrush ( QColor(255, 100, 0, 80) );
      p.drawEllipse(QPoint(px, py), radius,  radius);
      
      p.setPen   ( QColor(255, 100, 0, 0) );
      p.setBrush ( QColor(255, 100, 0, 255) );
      p.drawEllipse(QPoint(px, py), radius1, radius1);
      
      //p.setPen   ( QColor(255, 0, 0, 160) );
      //p.setBrush ( QColor(255, 0, 0, 160) );
      //p.drawEllipse(QPoint(px, py), radiusMin, radiusMin);
      
      double alpha = mTargetAzimuth * M_PI / 180;
      double sinA  = sin(alpha);
      double cosA  = cos(alpha);
      
      //QPointF P(px, py);
      //QPointF Q(px + radius * sinA, py - radius * cosA);
      //QPointF R(px - radius * cosA * 0.66 - radius * sinA * 0.25, py - radius * sinA * 0.66 + radius * cosA * 0.25);
      //QPointF S(px + radius * cosA * 0.66 - radius * sinA * 0.25, py + radius * sinA * 0.66 + radius * cosA * 0.25);
      
      QPointF P(px - radius1 * sinA * 0.22, py + radius1 * cosA * 0.22);
      QPointF Q(px + radius1 * sinA * 0.55, py - radius1 * cosA * 0.55);
      QPointF R(px + radius1 * cosA * 0.44 - radius1 * sinA * 0.55, py + radius1 * sinA * 0.44 + radius1 * cosA * 0.55);
      QPointF S(px - radius1 * cosA * 0.44 - radius1 * sinA * 0.55, py - radius1 * sinA * 0.44 + radius1 * cosA * 0.55);
    
      QPainterPath path;
      path.moveTo(Q);
      path.lineTo(R);
      path.lineTo(P);
      path.lineTo(S);
      path.lineTo(Q);
      p.fillPath(path, QBrush(QColor(255, 255, 255, 255)));
    }
  }
  
  // Drawing info panel
  if (!mInfoText.isEmpty())
  {
    QStringList lines = mInfoText.split("\n");
    
    QFont fixedFont = defaultFont;
    fixedFont.setFamily("Courier New");
    p.setFont(fixedFont);
    
    QFontMetrics fm(p.font());
    int fh = fm.height();
    int rh = lines.size() * (fh + 1);
    int rw = 0;
    
    for(int i = 0; i < lines.size(); ++i)
      rw = qMax(rw, fm.width(lines[i]) + 10);
    
    int rw1 = rw + 50 - (rw % 50);
    p.fillRect(0, 0, rw1, rh, QColor(255, 255, 255, 128));
    
    p.setPen(QColor(Qt::black));
    for(int i = 0; i < lines.size(); ++i)
      p.drawText(5, (i + 1) * (fh + 1), lines[i]);
  }
  
  // Drawing scale
  if (true)
  {
    const int minLen  = 100;  // minimum scale length
    const int padding = 10;   // padding from the bottom-left corner of the widget
    const double a = PARALLEL_DEG_LENGTH / mDegLength; // number of meters in 1 pixel
    
    QList<double> scales;
    scales << 1e0 << 2e0 << 3e0 << 4e0 << 5e0 << 6e0 << 7e0 << 8e0 << 9e0
           << 1e1 << 2e1 << 3e1 << 4e1 << 5e1 << 6e1 << 7e1 << 8e1 << 9e1
           << 1e2 << 2e2 << 3e2 << 4e2 << 5e2 << 6e2 << 7e2 << 8e2 << 9e2
           << 1e3 << 2e3 << 3e3 << 4e3 << 5e3 << 6e3 << 7e3 << 8e3 << 9e3
           << 1e4 << 2e4 << 3e4 << 4e4 << 5e4 << 6e4 << 7e4 << 8e4 << 9e4
           << 1e5 << 2e5 << 3e5 << 4e5 << 5e5 << 6e5 << 7e5 << 8e5 << 9e5
           << 1e6 << 2e6 << 3e6 << 4e6 << 5e6 << 6e6 << 7e6 << 8e6 << 9e6;
    
    double scale = scales.last();
    for(int i = 0; i < scales.size(); ++i)
      if (a * minLen < scales[i])
      {
        scale = scales[i];
        break;
      }
    
    // Calculating scale length in pixels
    int pxLen = qRound(scale / a);
    
    p.setPen(QColor(0, 0, 0));
    p.drawLine(padding, height() - padding, padding + pxLen, height() - padding);
    p.drawLine(padding, height() - padding, padding, height() - padding - 5);
    p.drawLine(padding + pxLen / 2, height() - padding, padding + pxLen / 2, height() - padding - 5);
    p.drawLine(padding + pxLen, height() - padding, padding + pxLen, height() - padding - 5);
    
    QString text0, text1, text2;
    if (scale < 1000)
    {
      text0 = QString("%1").arg(scale, 0, 'f', 0);
      text1 = text0 + " m";
      text2 = QString("%1").arg(scale/2, 0, 'f', static_cast<int>(scale) % 2);
    }
    else
    {
      text0 = QString("%1").arg(scale/1000, 0, 'f', 0);
      text1 = text0 + " km";
      text2 = QString("%1").arg(scale/2000, 0, 'f', static_cast<int>(scale/1000) % 2);
    }
    
    QFont font(p.font());
    
    QFont scaleFont = defaultFont;
    scaleFont.setFamily("Courier New");
    p.setFont(scaleFont);
    
    QFontMetrics fm(scaleFont);
    
    const int textY = height() - padding - fm.height() / 2;
    p.drawText(padding + pxLen - fm.width(text0) / 2, textY, text1);
    p.drawText(padding + pxLen / 2 - fm.width(text2) / 2, textY, text2);
  }
  
  p.end();
  event->accept();
}

QList<QRectF> CheckRectCoverage(const QRectF& A, const QList<QRectF>& B)
{
  QList<QRectF> queue;
  
  queue.append(A);
  for(int i = 0; i < B.size(); ++i)
  {
    if (queue.isEmpty())
      break;
    
    QList<QRectF> S;
    for(int j = 0; j < queue.size(); ++j)
    {
      QRectF R = queue[j];
      if (B[i].contains(R))
      {
        queue.removeAt(j--);
        continue;
      }
      if (B[i].intersects(R))
      {
        queue.removeAt(j--);
        if (B[i].top() > R.top() && B[i].top() < R.bottom())
        {
          QRectF R1(R);
          R1.setBottom(B[i].top());
          R.setTop(B[i].top());
          S.append(R1);
        }
        if (B[i].left() > R.left() && B[i].left() < R.right())
        {
          QRectF R1(R);
          R1.setRight(B[i].left());
          R.setLeft(B[i].left());
          S.append(R1);
        }
        if (B[i].right() > R.left() && B[i].right() < R.right())
        {
          QRectF R1(R);
          R1.setLeft(B[i].right());
          R.setRight(B[i].right());
          S.append(R1);
        }
        if (B[i].bottom() > R.top() && B[i].bottom() < R.bottom())
        {
          QRectF R1(R);
          R1.setTop(B[i].bottom());
          R.setBottom(B[i].bottom());
          S.append(R1);
        }
      }
    }
    queue.append(S);
  }
    
  return queue;
}

void QGoogleMap::refresh()
{
  const double LATITUDE_COEF = 1.0 / cos(mLatitude * M_PI / 180);
  
  // Searching for uncovered area
  QList<QRectF> rects;
  
  int paddingX = width()  / 2;
  int paddingY = height() / 2;
  
  // Analysing map chunks
  for(auto chunk: mMapChunks)
  {
    if (chunk.zoom != mMapZoom)
      continue;
    
    // Calculating pixel coordinates of the image center
    double dx = (chunk.longitude - mLongitude);
    double dy = (chunk.latitude  - mLatitude);
    qint64 px = width()  / 2 + (qint64)round(dx * mDegLength) - chunk.image.width()  / 2;
    qint64 py = height() / 2 - (qint64)round(dy * mDegLength * LATITUDE_COEF) - chunk.image.height() / 2;
    
    if (px > -paddingX - chunk.image.width()  && px < width()  + paddingX &&
        py > -paddingY - chunk.image.height() && py < height() + paddingY)
      rects.append(QRectF(px, py, chunk.image.width(), chunk.image.height()));
  }
  
  QList<QRectF> uncovered = CheckRectCoverage(QRectF(-paddingX, -paddingY, width() + 2 * paddingX, height() + 2 * paddingY), rects);
  
  int maxWidth  = 640;
  int maxHeight = 560;
  
  for(int i = 0; i < uncovered.size(); ++i)
  {
    QRectF R = uncovered[i];
    if (R.width() > maxWidth)
    {
      QRectF R1 = R;
      R1.setRight(R.left() + maxWidth);
      R.setLeft(R.left() + maxWidth);
      uncovered[i] = R;
      uncovered.append(R1);
    }
    if (R.height() > maxHeight)
    {
      QRectF R1 = R;
      R1.setBottom(R.top() + maxHeight);
      R.setTop(R.left() + maxHeight);
      uncovered[i] = R;
      uncovered.append(R1);
    }
  }
  
  for(int i = 0; i < uncovered.size(); ++i)
  {
    int px = uncovered[i].left();
    int py = uncovered[i].top();
    double dx = (px - width() / 2)  / mDegLength;
    double dy = (height() / 2 - py) / mDegLength / LATITUDE_COEF;
    double longitude = dx + mLongitude;
    double latitude  = dy + mLatitude;
    
    requestMap(latitude, longitude, mMapZoom);
  }
  
  if (mMapChunks.size() > MEM_CACHE_SIZE)
    clearCache();
  
  if (mAdjustButton->isChecked() && hasTarget())
  {
    QDateTime timeNow = QDateTime::currentDateTime();
    if (timeNow > mAdjustTime)
    {
      mLatitude   = mTargetLatitude;
      mLongitude  = mTargetLongitude;
    }
  }
}

void QGoogleMap::clearCache()
{
  const double LATITUDE_COEF = 1.0 / cos(mLatitude * M_PI / 180);
  
  const int paddingX = width()  / 2;
  const int paddingY = height() / 2;
  
  // Analysing map chunks
  for(auto iter = mMapChunks.begin(); iter != mMapChunks.end(); )
  {
    const MapChunk& chunk = iter.value();
    if (chunk.zoom != mMapZoom)
    {
      mMapChunks.erase(iter++);
      continue;
    }

    // Calculating pixel coordinates of the image center
    double dx = (chunk.longitude - mLongitude);
    double dy = (chunk.latitude  - mLatitude);
    qint64 px = width()  / 2 + (qint64)round(dx * mDegLength) - chunk.image.width()  / 2;
    qint64 py = height() / 2 - (qint64)round(dy * mDegLength * LATITUDE_COEF) - chunk.image.height() / 2;
    
    if (px <= -paddingX - chunk.image.width()  || px >= width()  + paddingX ||
        py <= -paddingY - chunk.image.height() || py >= height() + paddingY)
    {
      mMapChunks.erase(iter++);
      continue;
    }
    ++iter;
  }
}

void QGoogleMap::onZoomIn()
{
  if (mMapZoom < ZOOM_MAX)
  {
    ++mMapZoom;
    mDegLength *= 2;
    mZoomInButton ->setEnabled(mMapZoom < ZOOM_MAX);
    mZoomOutButton->setEnabled(mMapZoom > ZOOM_MIN);
  }
  update();
}

void QGoogleMap::onZoomOut()
{
  if (mMapZoom > ZOOM_MIN)
  {
    --mMapZoom;
    mDegLength /= 2;
    mZoomInButton ->setEnabled(mMapZoom < ZOOM_MAX);
    mZoomOutButton->setEnabled(mMapZoom > ZOOM_MIN);
  }
  update();
}

void QGoogleMap::onScroll(int px, int py)
{
  const double LATITUDE_COEF = 1.0 / cos(mLatitude * M_PI / 180);
  mLatitude  += py / mDegLength / LATITUDE_COEF;
  mLongitude -= px / mDegLength;
  update();
  
  mAdjustTime = QDateTime::currentDateTime().addSecs(5);
}

void QGoogleMap::requestMap(double lat, double lon, int zoom)
{
  // Rounding latitude and longitude to 0.001
  lat = round(lat * 1000) / 1000;
  lon = round(lon * 1000) / 1000;
  
  QString hash("%1,%2,%3");
  hash = hash.arg(zoom);
  hash = hash.arg(lat, 0, 'f', 6);
  hash = hash.arg(lon, 0, 'f', 6);
  
  if (mMapChunks.contains(hash))
    return;
  
  // Requesting cache storage
  QString fileName("%1/%2-%3.png");
  fileName = fileName.arg(mHomeDir + "/cache");
  fileName = fileName.arg(mMapType);
  fileName = fileName.arg(hash);
  
  QImage image;
  if (image.load(fileName))
  {
    // Updating file timestamp
    utime(qPrintable(fileName), 0);
    
    MapChunk chunk;
    chunk.type = mMapType;
    chunk.zoom = zoom;
    chunk.latitude  = lat;
    chunk.longitude = lon;
    chunk.image = image.copy(0, 40, image.width(), image.height() - 80);
    mMapChunks[hash] = chunk;
    update();
    return;
  }
  
  mMapChunks.insert(hash, MapChunk());
  
  // Requesting google api service
  QString url("https://maps.googleapis.com/maps/api/staticmap?center=%1,%2&zoom=%3&size=640x640&maptype=%4&key=%5");
  url = url.arg(lat, 0, 'f', 6);
  url = url.arg(lon, 0, 'f', 6);
  url = url.arg(zoom);
  url = url.arg(mMapType);
  url = url.arg(mApiKey);
  
  qDebug() << "Requesting " << hash << ", cached: " << mMapChunks.size();
  QNetworkReply* reply = mNetworkManager->get(QNetworkRequest(QUrl(url)));
  reply->setProperty("type", QString("request_map"));
  reply->setProperty("hash", hash);
  
  QTimer* requestTimer = new QTimer(reply);
  requestTimer->setObjectName("request_timer");
  requestTimer->setSingleShot(true);
  requestTimer->setInterval(5000);
  requestTimer->start();
  connect(requestTimer, SIGNAL(timeout()), mNetworkTimeoutSignalMapper, SLOT(map()));
  mNetworkTimeoutSignalMapper->setMapping(requestTimer, reply);  
}

void QGoogleMap::onRequestFinished(QNetworkReply* reply)
{
  const QString url     = reply->url().toString();
  const QString type    = reply->property("type").toString();
  const QString hash    = reply->property("hash").toString();
  const int errorCode   = reply->error();
  const int statusCode  = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QByteArray data = reply->readAll();
  
  if (errorCode == QNetworkReply::NoError)
  {
    if (type == "request_map")
    {
      QStringList values = hash.split(",");
      const int    zoom      = values[0].toInt();
      const double latitude  = values[1].toDouble();
      const double longitude = values[2].toDouble();
      
      // Caching file
      QString fileName("%1/%2-%3.png");
      fileName = fileName.arg(mHomeDir + "/cache");
      fileName = fileName.arg(mMapType);
      fileName = fileName.arg(hash);
      
      QFile f(fileName);
      if (f.open(QIODevice::WriteOnly))
      {
        f.write(data);
        f.close();
      }
      
      //QDir dir(mHomeDir + "/cache");
      //QStringList fileList = dir.entryList(QStringList() << "*.png", QDir::Files, QDir::Time);
      //while (fileList.size() > DISK_CACHE_SIZE)
      //{
      //  //qDebug() << "Removing" << fileList.back();
      //  //dir.remove(fileList.back());
      //  dir.rename(fileList.last(), fileList.last() + ".tmp");
      //  fileList.removeLast();
      //}
      
      QImage image;
      if (image.loadFromData(data))
      {
        MapChunk chunk;
        chunk.type = mMapType;
        chunk.zoom = zoom;
        chunk.latitude  = latitude;
        chunk.longitude = longitude;
        chunk.image = image.copy(0, 40, image.width(), image.height() - 80);
        mMapChunks[hash] = chunk;
        update();
      }
    }
  }
  else
  {
    qDebug() << "Request " << type << ": FAILED with error " << reply->errorString();
    if (type == "request_map")
      mMapChunks.remove(hash);
  }
  
  QTimer* timer = reply->findChild<QTimer*>("request_timer");
  if (timer)
    timer->stop();
  
  reply->deleteLater();
}

void QGoogleMap::onRequestTimeout(QObject* reply)
{
  dynamic_cast<QNetworkReply*>(reply)->abort();
}

static double getTimeStamp()
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void QGoogleMap::onReadLine(QString line)
{
  QDateTime timeNow = QDateTime::currentDateTime();
  QStringList parts = line.split(" ");
  if (parts.size() == 17)
  {
    // Line format:
    // time gx gy gz ax ay az odo_count odo_speed gps_count lat lon alt acc gprmc_count vel dir
    double timestamp = parts[0].toDouble();
    double latency   = getTimeStamp() - timestamp;
    
    double gx = parts[1].toDouble();
    double gy = parts[2].toDouble();
    double gz = parts[3].toDouble();
    double ax = parts[4].toDouble();
    double ay = parts[5].toDouble();
    double az = parts[6].toDouble();
    
    double odometer = parts[8].toDouble();
    
    double latitude  = parts[10].toDouble();
    double longitude = parts[11].toDouble();
    double altitude  = parts[12].toDouble();
    double accuracy  = parts[13].toDouble();
    
    double velocity  = parts[15].toDouble();
    double direction = parts[16].toDouble();
    
    double gps_count = parts[9].toDouble();
    if (gps_count > EPSILON)
      mGpsTime = timeNow;
    
    setTarget(latitude, longitude, accuracy, direction);
    
    QMap<QString,QString> addressMap;
    QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for(int i = 0; i < interfaces.size(); ++i)
    {
      QList<QNetworkAddressEntry> addresses = interfaces[i].addressEntries();
      for(int j = 0; j < addresses.size(); ++j)
        if (addresses[j].ip().protocol() == QAbstractSocket::IPv4Protocol)
          addressMap[interfaces[i].name()] = addresses[j].ip().toString();
    }
    
    QString text;
    
    text += QString("IP address : %1\n").arg(addressMap.value("wlan0"));
    
    text += QString("Latency    : %1\n").arg(latency, 0, 'f', 3);
    
    text += QString("Location   : %1, %2, %3\n")
              .arg(latitude,  0, 'f', 6)
              .arg(longitude, 0, 'f', 6)
              .arg(altitude,  0, 'f', 1);
    
    text += QString("Direction  : %1\n").arg(direction,  0, 'f', 2);
    text += QString("Velocity   : %1\n").arg(velocity,   0, 'f', 2);
    text += QString("Odometer   : %1\n").arg(odometer,   0, 'f', 2);
    text += QString("Accel      : %1, %2, %3\n").arg(ax, 0, 'f', 0).arg(ay, 0, 'f', 0).arg(az, 0, 'f', 0);
    text += QString("Gyro       : %1, %2, %3\n").arg(gx, 0, 'f', 0).arg(gy, 0, 'f', 0).arg(gz, 0, 'f', 0);
    
    qint64 gpsDelta = mGpsTime.msecsTo(timeNow);
    if (gpsDelta < 3000)
      text += QString("GPS        : on\n");
    else
      text += QString("GPS        : off (%1 sec)\n").arg(gpsDelta / 1000);
    
    setInfoText(text);
    
    if (!mRecordLogFile.isEmpty())
    {
      QFile f(mRecordLogFile);
      if (f.open(QIODevice::Append))
      {
        QString text("%1 %2 %3 %4\n");
        text = text.arg(timeNow.toString("yyyy-MM-dd hh:mm:ss.zzz"));
        text = text.arg(latitude,  0, 'f', 6);
        text = text.arg(longitude, 0, 'f', 6);
        text = text.arg(gpsDelta / 1000);
        f.write(text.toUtf8());
        f.close();
      }
    }
    
    //qDebug() << qPrintable(text) << "\n";
  }
}

void QGoogleMap::onAdjustModeToggle()
{
  mAdjustTime = QDateTime::currentDateTime();
  mAdjustButton->setIcon(mAdjustButton->isChecked() ? QIcon(":/icons/adjust_mode_on") : QIcon(":/icons/adjust_mode_off"));
}

void QGoogleMap::onRecordToggle()
{
  mRecordButton->setIcon(mRecordButton->isChecked() ? QIcon(":/icons/record_stop") : QIcon(":/icons/record_start"));
  
  if (!mRecordProcess)
  {
    QDateTime timeNow = QDateTime::currentDateTime();
    // Creating new process
    mRecordVideoFile = QString("%1/%2.mp4");
    mRecordVideoFile = mRecordVideoFile.arg(mHomeDir + "/video");
    mRecordVideoFile = mRecordVideoFile.arg(timeNow.toString("yyyyMMdd_hhmmss"));
    
    mRecordLogFile = QString("%1/%2.log");
    mRecordLogFile = mRecordLogFile.arg(mHomeDir + "/logs");
    mRecordLogFile = mRecordLogFile.arg(timeNow.toString("yyyyMMdd_hhmmss"));
    
    qDebug() << "Start recording video" << mRecordVideoFile;
    qDebug() << "Start recording video" << mRecordVideoFile;
    
    QPoint P = this->mapToGlobal(QPoint(0,0));
    QString command("%1 -f x11grab -r 25 -s %2x%3 -i :0.0+%4,%5 -vcodec h264 %6");
    command = command.arg(FFMPEG);
    command = command.arg(width());
    command = command.arg(height());
    command = command.arg(P.x());
    command = command.arg(P.y());
    command = command.arg(mRecordVideoFile);
    
    mRecordProcess = new QProcess(this);
    mRecordProcess->start(command);
    connect(mRecordProcess, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(onRecordFinished()));
  }
  else
  {
    // Stopping existing process
    qDebug() << "Stop recording video" << mRecordVideoFile;
    kill(mRecordProcess->pid(), SIGINT);
    mRecordButton->blockSignals(true);
    mRecordVideoFile.clear();
    mRecordLogFile.clear();
  }
}

void QGoogleMap::onRecordFinished()
{
  mRecordButton->blockSignals(false);
  delete mRecordProcess;
  mRecordProcess = 0;
}

int main(int argc, char** argv)
{
  QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
  setlocale(LC_NUMERIC, "C");
  
  if (argc < 2)
  {
    qCritical() << "Missing api-key file";
    return -1;
  }
  
  QFile f(argv[1]);
  if (!f.open(QIODevice::ReadOnly))
  {
    qCritical() << "Unable to read api-key file" << argv[1];
    return -1;
  }
  QString apiKey = QString(f.readAll()).trimmed();
  f.close();
  
  QApplication app(argc, argv);
  
  QGoogleMap* map = new QGoogleMap(apiKey);
  map->setMinimumSize(800, 480);
  map->show();
  return app.exec();
}
