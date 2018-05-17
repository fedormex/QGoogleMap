#include "QGoogleMap.h"

const int     CACHE_SIZE_MAX  = 500;  // maximum number of chunks
const int     ZOOM_MAX        = 18;   // maximum zoom value
const int     ZOOM_MIN        = 10;   // minimum zoom value
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
    emit readLine(line);
  }
}

QGoogleMap::QGoogleMap(const QString& apiKey, QWidget* parent)
  : QWidget          ( parent )
  , mApiKey          ( apiKey )
  , mMapType         ( "roadmap" )
  , mMapZoom         ( ZOOM_MAX  )
  , mDegLength       ( DEG_LENGTH_ARRAY[mMapZoom] )
  , mLatitude        ( 55.754 )
  , mLongitude       ( 37.620 )
  , mTargetLatitude  ( 0.0 )
  , mTargetLongitude ( 0.0 )
  , mTargetAccuracy  ( 0.0 )
  , mAdjustMode      ( true )
  , mAdjustTime      ( QDateTime::currentDateTime() )
{
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
  mAdjustButton->setStyleSheet("background-color: rgba(200,200,200,200); border-style: solid; border-radius: 30px; ");
  mAdjustButton->setIcon(mAdjustMode ? QIcon(":/icons/adjust_mode_on") : QIcon(":/icons/adjust_mode_off"));
  mAdjustButton->setFixedSize(60, 60);
  connect(mAdjustButton, SIGNAL(clicked()), this, SLOT(onAdjustModeToggle()));
}

void QGoogleMap::setTarget(double latitude, double longitude, double accuracy)
{
  mTargetLatitude  = latitude;
  mTargetLongitude = longitude;
  mTargetAccuracy  = accuracy;
  
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
  
  mZoomInButton  -> move(width() - buttonWidth - 10, height() / 2 - padding / 2 - buttonHeight);
  mZoomOutButton -> move(width() - buttonWidth - 10, height() / 2 + padding / 2);
  mAdjustButton  -> move(width() - buttonWidth - 10, height() - padding - buttonHeight);
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
  const double LATITUDE_COEF = 1.0 / cos(mLatitude * M_PI / 180);
  
  QPainter p;
  p.begin(this);
  
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
    double dx = mTargetLongitude - mLongitude;
    double dy = mTargetLatitude  - mLatitude;
    qint64 px = width()  / 2 + (qint64)round(dx * mDegLength);
    qint64 py = height() / 2 - (qint64)round(dy * mDegLength * LATITUDE_COEF);
    
    int radiusMin = 10;
    int radius    = std::max((int)round(mTargetAccuracy * 10 * LATITUDE_COEF * mDegLength / 111111.111111), radiusMin);
    
    if (px >= -100 && px < width()  + 100 &&
        py >= -100 && py < height() + 100)
    {
      p.setPen   ( QColor(255, 0, 0, 80) );
      p.setBrush ( QColor(255, 0, 0, 80) );
      p.drawEllipse(QPoint(px, py), radius, radius);
      
      p.setPen   ( QColor(255, 0, 0, 160) );
      p.setBrush ( QColor(255, 0, 0, 160) );
      p.drawEllipse(QPoint(px, py), radiusMin, radiusMin);
    }
  }
  
  // Drawing info panel
  if (!mInfoText.isEmpty())
  {
    QStringList lines = mInfoText.split("\n");
    
    QFont fixedFont = font();
    fixedFont.setFamily("Courier New");
    p.setFont(fixedFont);
    
    QFontMetrics fm(p.font());
    int fh = fm.height();
    int rh = lines.size() * (fh + 1);
    int rw = 0;
    
    for(int i = 0; i < lines.size(); ++i)
      rw = qMax(rw, fm.width(lines[i]) + 10);
    
    p.fillRect(0, 0, rw, rh, QColor(255, 255, 255, 200));
    
    p.setPen(QColor(Qt::black));
    for(int i = 0; i < lines.size(); ++i)
      p.drawText(5, (i + 1) * (fh + 1), lines[i]);
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
  
  if (mMapChunks.size() > CACHE_SIZE_MAX)
    clearCache();
  
  if (mAdjustMode && hasTarget())
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
  qDebug() << "Requesting " << hash << ", cached: " << mMapChunks.size();
  
  QString url("https://maps.googleapis.com/maps/api/staticmap?center=%1,%2&zoom=%3&size=640x640&maptype=%4&key=%5");
  url = url.arg(lat, 0, 'f', 6);
  url = url.arg(lon, 0, 'f', 6);
  url = url.arg(zoom);
  url = url.arg(mMapType);
  url = url.arg(mApiKey);
  
  if (mMapChunks.contains(hash))
    return;
  
  mMapChunks.insert(hash, MapChunk());
  
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
      //qDebug() << hash;
      
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

void QGoogleMap::onReadLine(QString line)
{
  QStringList parts = line.split(" ");
  if (parts.size() == 17)
  {
    // Line format:
    // time gx gy gz ax ay az odo_count odo_speed gps_count lat lon alt acc gprmc_count vel dir
    
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
    
    setTarget(latitude, longitude, accuracy);
    
    QString text;
    text += QString("Latitude:  %1\n").arg(latitude,  0, 'f', 6);
    text += QString("Longitude: %1\n").arg(longitude, 0, 'f', 6);
    text += QString("Altitude:  %1\n").arg(altitude,  0, 'f', 2);
    text += QString("Accuracy:  %1\n").arg(accuracy,  0, 'f', 2);
    text += QString("Velocity:  %1\n").arg(velocity,  0, 'f', 2);
    text += QString("Direction: %1\n").arg(direction, 0, 'f', 2);
    text += QString("Odometer:  %1\n").arg(odometer,  0, 'f', 2);
    text += QString("Accel:     %1, %2, %3\n").arg(ax, 0, 'f', 1).arg(ay, 0, 'f', 1).arg(az, 0, 'f', 1);
    text += QString("Gyro:      %1, %2, %3\n").arg(gx, 0, 'f', 1).arg(gy, 0, 'f', 1).arg(gz, 0, 'f', 1);
    setInfoText(text);
    
    qDebug() << text;
  }
}

void QGoogleMap::onAdjustModeToggle()
{
  mAdjustMode = !mAdjustMode;
  mAdjustTime = QDateTime::currentDateTime();
  mAdjustButton->setIcon(mAdjustMode ? QIcon(":/icons/adjust_mode_on") : QIcon(":/icons/adjust_mode_off"));
}

int main(int argc, char** argv)
{
  QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
  
  QApplication app(argc, argv);
  setlocale(LC_NUMERIC, "C");
  
  QGoogleMap* map = new QGoogleMap("AIzaSyAuzQq_7hOhT0-HuaDrGsdzOooxRFTfcWM");
  map->setMinimumSize(800, 480);
  map->show();
  return app.exec();
}
