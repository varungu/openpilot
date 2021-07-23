#include "selfdrive/ui/qt/offroad/networking.h"

#include <algorithm>

#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScrollBar>

#include "selfdrive/ui/qt/util.h"
#include "selfdrive/ui/qt/qt_window.h"
#include "selfdrive/ui/qt/widgets/scrollview.h"


// Networking functions

Networking::Networking(QWidget* parent, bool show_advanced) : QFrame(parent) {
  main_layout = new QStackedLayout(this);

  wifi = new WifiManager(this);
  connect(wifi, &WifiManager::refreshSignal, this, &Networking::refresh);
  connect(wifi, &WifiManager::wrongPassword, this, &Networking::wrongPassword);

  QWidget* wifiScreen = new QWidget(this);
  QVBoxLayout* vlayout = new QVBoxLayout(wifiScreen);
  vlayout->setContentsMargins(20, 20, 20, 20);
  if (show_advanced) {
    QPushButton* advancedSettings = new QPushButton("Advanced");
    advancedSettings->setObjectName("advancedBtn");
    advancedSettings->setStyleSheet("margin-right: 30px;");
    advancedSettings->setFixedSize(350, 100);
    connect(advancedSettings, &QPushButton::clicked, [=]() { main_layout->setCurrentWidget(an); });
    vlayout->addSpacing(10);
    vlayout->addWidget(advancedSettings, 0, Qt::AlignRight);
    vlayout->addSpacing(10);
  }

  wifiWidget = new WifiUI(this, wifi);
  wifiWidget->setObjectName("wifiWidget");
  connect(wifiWidget, &WifiUI::connectToNetwork, this, &Networking::connectToNetwork);

  ScrollView *wifiScroller = new ScrollView(wifiWidget, this);
  wifiScroller->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  vlayout->addWidget(wifiScroller, 1);
  main_layout->addWidget(wifiScreen);

  an = new AdvancedNetworking(this, wifi);
  connect(an, &AdvancedNetworking::backPress, [=]() { main_layout->setCurrentWidget(wifiScreen); });
  main_layout->addWidget(an);

  QPalette pal = palette();
  pal.setColor(QPalette::Background, QColor(0x29, 0x29, 0x29));
  setAutoFillBackground(true);
  setPalette(pal);

  // TODO: revisit pressed colors
  setStyleSheet(R"(
    #wifiWidget > QPushButton, #back_btn, #advancedBtn {
      font-size: 50px;
      margin: 0px;
      padding: 15px;
      border-width: 0;
      border-radius: 30px;
      color: #dddddd;
      background-color: #444444;
    }
  )");
  main_layout->setCurrentWidget(wifiScreen);
}

void Networking::refresh() {
  wifiWidget->refresh();
  an->refresh();
}

void Networking::connectToNetwork(const Network &n) {
  if (wifi->isKnownConnection(n.ssid)) {
    wifi->activateWifiConnection(n.ssid);
  } else if (n.security_type == SecurityType::OPEN) {
    wifi->connect(n);
  } else if (n.security_type == SecurityType::WPA) {
    QString pass = InputDialog::getText("Enter password", this, "for \"" + n.ssid + "\"", true, 8);
    if (!pass.isEmpty()) {
      wifi->connect(n, pass);
    }
  }
}

void Networking::wrongPassword(const QString &ssid) {
  for (Network n : wifi->seen_networks) {
    if (n.ssid == ssid) {
      QString pass = InputDialog::getText("Wrong password", this, "for \"" + n.ssid +"\"", true, 8);
      if (!pass.isEmpty()) {
        wifi->connect(n, pass);
      }
      return;
    }
  }
}

void Networking::showEvent(QShowEvent* event) {
  // Wait to refresh to avoid delay when showing Networking widget
  QTimer::singleShot(300, this, [=]() {
    if (this->isVisible()) {
      wifi->refreshNetworks();
      refresh();
    }
  });
}

// AdvancedNetworking functions

AdvancedNetworking::AdvancedNetworking(QWidget* parent, WifiManager* wifi): QWidget(parent), wifi(wifi) {

  QVBoxLayout* main_layout = new QVBoxLayout(this);
  main_layout->setMargin(40);
  main_layout->setSpacing(20);

  // Back button
  QPushButton* back = new QPushButton("Back");
  back->setObjectName("back_btn");
  back->setFixedSize(500, 100);
  connect(back, &QPushButton::clicked, [=]() { emit backPress(); });
  main_layout->addWidget(back, 0, Qt::AlignLeft);

  // Enable tethering layout
  ToggleControl *tetheringToggle = new ToggleControl("Enable Tethering", "", "", wifi->isTetheringEnabled());
  main_layout->addWidget(tetheringToggle);
  QObject::connect(tetheringToggle, &ToggleControl::toggleFlipped, this, &AdvancedNetworking::toggleTethering);
  main_layout->addWidget(horizontal_line(), 0);

  // Change tethering password
  ButtonControl *editPasswordButton = new ButtonControl("Tethering Password", "EDIT");
  connect(editPasswordButton, &ButtonControl::clicked, [=]() {
    QString pass = InputDialog::getText("Enter new tethering password", this, "", true, 8, wifi->getTetheringPassword());
    if (!pass.isEmpty()) {
      wifi->changeTetheringPassword(pass);
    }
  });
  main_layout->addWidget(editPasswordButton, 0);
  main_layout->addWidget(horizontal_line(), 0);

  // IP address
  ipLabel = new LabelControl("IP Address", wifi->ipv4_address);
  main_layout->addWidget(ipLabel, 0);
  main_layout->addWidget(horizontal_line(), 0);

  // SSH keys
  main_layout->addWidget(new SshToggle());
  main_layout->addWidget(horizontal_line(), 0);
  main_layout->addWidget(new SshControl());

  main_layout->addStretch(1);
}

void AdvancedNetworking::refresh() {
  ipLabel->setText(wifi->ipv4_address);
  update();
}

void AdvancedNetworking::toggleTethering(bool enabled) {
  wifi->setTetheringEnabled(enabled);
}

// WifiUI functions

WifiUI::WifiUI(QWidget *parent, WifiManager* wifi) : QWidget(parent), wifi(wifi) {
//  installEventFilter(this);
  main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(0);

  // load imgs
  for (const auto &s : {"low", "medium", "high", "full"}) {
    QPixmap pix(ASSET_PATH + "/offroad/icon_wifi_strength_" + s + ".svg");
    strengths.push_back(pix.scaledToHeight(68, Qt::SmoothTransformation));
  }
  lock = QPixmap(ASSET_PATH + "offroad/icon_lock_closed.svg").scaledToWidth(49, Qt::SmoothTransformation);
  checkmark = QPixmap(ASSET_PATH + "offroad/icon_checkmark.svg").scaledToWidth(49, Qt::SmoothTransformation);

//  QLabel *scanning = new QLabel("Scanning for networks...");
//  scanning->setStyleSheet("font-size: 65px;");
//  main_layout->addWidget(scanning, 0, Qt::AlignCenter);

  setStyleSheet(R"(
    QScrollBar::handle:vertical {
      min-height: 0px;
      border-radius: 4px;
      background-color: #8A8A8A;
    }
    #forgetBtn {
      font-size: 32px;
      font-weight: 600;
      color: #292929;
      background-color: #BDBDBD;
      border-width: 1px solid #828282;
      border-radius: 5px;
      padding: 40px;
      padding-bottom: 16px;
      padding-top: 16px;
    }
  )");
}

//bool WifiUI::eventFilter(QObject *obj, QEvent *event) {
////  qDebug() << obj;
//  qDebug() << event;
//
//  const static QSet<QEvent::Type> mouse_events({QEvent::MouseButtonPress, QEvent::MouseButtonRelease, QEvent::MouseMove});
//  if (mouse_events.contains(event->type())) {
//    QWidget *w = qobject_cast<QWidget*>(obj);
//    if (w && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove))
//    w->grabMouse();
//    if (w && event->type() == QEvent::QEvent::MouseButtonRelease)
//    w->releaseMouse();
//    qDebug() << obj->metaObject()->className() << event;
//    return QWidget::eventFilter(obj, event);
//  }
//
//  return false;
//}

//bool WifiUI::event(QEvent *event) {
////  qDebug() << event->type();
//  return QWidget::event(event);
//}

QWidget *WifiUI::newNetworkWidget(const Network &network) {
  QHBoxLayout *hlayout = new QHBoxLayout;
  hlayout->setContentsMargins(44, 0, 73, 0);
  hlayout->setSpacing(50);

  // Clickable SSID label
  QPushButton *ssid_label = new QPushButton(network.ssid);
  ssid_label->setProperty("ssid", network.ssid);
  ssid_label->setEnabled(network.connected != ConnectedType::CONNECTED &&
                         network.connected != ConnectedType::CONNECTING &&
                         network.security_type != SecurityType::UNSUPPORTED);
  int weight = network.connected == ConnectedType::DISCONNECTED ? 300 : 500;
  ssid_label->setStyleSheet(QString(R"(
    font-size: 55px;
    font-weight: %1;
    text-align: left;
    border: none;
    padding-top: 50px;
    padding-bottom: 50px;
  )").arg(weight));
  QObject::connect(ssid_label, &QPushButton::clicked, this, [=]() { emit connectToNetwork(network); });
  QObject::connect(ssid_label, &QPushButton::pressed, this, [=]() { currentButton = network.ssid; });
  QObject::connect(ssid_label, &QPushButton::released, this, [=]() { currentButton = ""; });
  hlayout->addWidget(ssid_label, network.connected == ConnectedType::CONNECTING ? 0 : 1);

  if (network.connected == ConnectedType::CONNECTING) {
    QPushButton *connecting = new QPushButton("CONNECTING...");
    connecting->setStyleSheet(R"(
      font-size: 32px;
      font-weight: 600;
      color: white;
      border-radius: 0;
      padding: 27px;
      padding-left: 43px;
      padding-right: 43px;
      background-color: black;
    )");
    hlayout->addWidget(connecting, 2, Qt::AlignLeft);
  }

  // Forget button
  if (wifi->isKnownConnection(network.ssid) && !wifi->isTetheringEnabled()) {
    QPushButton *forgetBtn = new QPushButton("FORGET");
    forgetBtn->setObjectName("forgetBtn");
    QObject::connect(forgetBtn, &QPushButton::clicked, [=]() {
      if (ConfirmationDialog::confirm("Forget WiFi Network \"" + QString::fromUtf8(network.ssid) + "\"?", this)) {
        wifi->forgetConnection(network.ssid);
      }
    });
    hlayout->addWidget(forgetBtn, 0, Qt::AlignRight);
  }

  // Status icon
  if (network.connected == ConnectedType::CONNECTED) {
    QLabel *connectIcon = new QLabel();
    connectIcon->setPixmap(checkmark);
    hlayout->addWidget(connectIcon, 0, Qt::AlignRight);
  } else if (network.security_type == SecurityType::WPA) {
    QLabel *lockIcon = new QLabel();
    lockIcon->setPixmap(lock);
    hlayout->addWidget(lockIcon, 0, Qt::AlignRight);
  } else {
    hlayout->addSpacing(lock.width() + hlayout->spacing());
  }

  // Strength indicator
  QLabel *strength = new QLabel();
  strength->setPixmap(strengths[std::clamp((int)network.strength/26, 0, 3)]);
  hlayout->addWidget(strength, 0, Qt::AlignRight);

  QWidget *network_widget = new QWidget;
  network_widget->setLayout(hlayout);
  return network_widget;
}


void WifiUI::refresh() {
  // TODO: don't rebuild this every time
//  clearLayout(main_layout);

  if (wifi->seen_networks.size() == 0) {
//    QLabel *scanning = new QLabel("Scanning for networks...");
//    scanning->setStyleSheet("font-size: 65px;");
//    main_layout->addWidget(scanning, 0, Qt::AlignCenter);
    return;
  }

  // add networks
  int i = 0;
  for (Network &network : wifi->seen_networks) {
    if (i < main_layout->count()) {
      QWidget *network_widget = newNetworkWidget(network);
      QPushButton *ssidLabel = qobject_cast<QPushButton*>(network_widget->layout()->itemAt(0)->widget());

//      main_layout->itemAt(i)->widget()->releaseMouse();
//      releaseMouse();
//      QWidget *widget = main_layout->takeAt(i)->widget();
//      main_layout->addWidget(network_widget);
      QWidget *widget = main_layout->replaceWidget(main_layout->itemAt(i)->widget(), network_widget)->widget();
      qDebug() << ssidLabel->property("ssid").toString() << currentButton;
      if (ssidLabel->property("ssid").toString() == currentButton) {
        qDebug() << "PRESSED:" << network.ssid;
        network_widget->grabMouse();
      }

      QTimer::singleShot(100, this, [=]() {
        clearLayout(widget->layout());
//        widget->deleteLater();
        delete widget;
      });
//      main_layout->replaceWidget(main_layout->itemAt(i)->widget(), network_widget);
//      main_layout->replaceWidget(main_layout->itemAt(i)->widget(), w); // TODO delete
//      clearLayout(widget->layout());
//      delete widget;
//      clearLayout(old->layout());
//      old->deleteLater();
//      delete old;
    } else {
      QWidget *w = newNetworkWidget(network);
      main_layout->addWidget(w);
//      setFocusPolicy(Qt::NoFocus);
//      w->setFocusPolicy(Qt::NoFocus);

    }



    // Don't add the last horizontal line
//    if (i+1 < wifi->seen_networks.size()) {
//      main_layout->addWidget(horizontal_line(), 0);
//    }
    i++;
  }

  qDebug() << i << main_layout->count();
  while (i < main_layout->count() - 1) {  // delete excess widgets
    QLayoutItem *item = main_layout->takeAt(i);
//    clearLayout(item->layout());
    delete item;
  }

  qDebug() << "------";
  main_layout->addStretch(1);
//  releaseMouse();
}
