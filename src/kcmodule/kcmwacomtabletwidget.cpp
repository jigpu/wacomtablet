/*
 * This file is part of the KDE wacomtablet project. For copyright
 * information and license terms see the AUTHORS and COPYING files
 * in the top-level directory of this distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "kcmwacomtabletwidget.h"
#include "ui_kcmwacomtabletwidget.h"
#include "ui_saveprofile.h"
#include "ui_errorwidget.h"
#include "debug.h"

#include "profilemanagement.h"
#include "generalpagewidget.h"
#include "styluspagewidget.h"
#include "buttonpagewidget.h"
#include "tabletpagewidget.h"
#include "touchpagewidget.h"

// common
#include "dbustabletinterface.h"
#include "devicetype.h"

//Qt includes
#include <QDBusReply>
#include <QPointer>
#include <QStringList>
#include <QPixmap>
#include <QLineEdit>
#include <QDebug>
#include <QInputDialog>
#include <QDialogButtonBox>
#include <QX11Info>
#include <QMessageBox>

using namespace Wacom;

namespace Wacom {
/**
  * Private class for the d-pointer.
  */
class KCMWacomTabletWidgetPrivate {
    public:
        Ui::KCMWacomTabletWidget ui;          //!< This user interface.

        GeneralPageWidget generalPage;        //!< Widget that shows some basic information about the tablet.
        StylusPageWidget  stylusPage;         //!< Widget for the pen settings (stylus/eraser).
        ButtonPageWidget  buttonPage;         //!< Widget for the express button settings.
        TabletPageWidget  tabletPage;         //!< Widget for the tablet settings.
        TouchPageWidget   touchPage;          //!< Widget for the touch settings.
        QWidget           deviceErrorWidget;  //!< Device error widget.
        Ui::ErrorWidget   deviceErrorUi;      //!< Device error widget ui.
        bool              profileChanged;     //!< True if the profile was changed and not saved yet.
}; // CLASS
}  // NAMESPACE



KCMWacomTabletWidget::KCMWacomTabletWidget( QWidget *parent )
    : QWidget( parent ), d_ptr(new KCMWacomTabletWidgetPrivate)
{
    setupUi();
    loadTabletInformation();
    showHideConfig();
}

KCMWacomTabletWidget::~KCMWacomTabletWidget()
{
    delete this->d_ptr;
}

void KCMWacomTabletWidget::setupUi()
{
    Q_D( KCMWacomTabletWidget );

    DBusTabletInterface* dbusTabletInterface = &DBusTabletInterface::instance();

    if( !dbusTabletInterface->isValid() ) {
        dbgWacom << "DBus interface not available";
    }

    d->profileChanged    = false;

    // setup error widget
    d->deviceErrorUi.setupUi(&(d->deviceErrorWidget));
    d->deviceErrorUi.errorImage->setPixmap( QIcon::fromTheme( QLatin1String( "dialog-warning" ) ).pixmap(48) );
    connect(d->deviceErrorUi.buttonRunTabletFinder, &QCommandLinkButton::clicked, this, &KCMWacomTabletWidget::showTabletFinder);
    d->deviceErrorUi.buttonRunTabletFinder->setVisible(false);

    // setup normal ui
    d->ui.setupUi( this );
    d->ui.addProfileButton->setIcon( QIcon::fromTheme( QLatin1String( "document-new" ) ) );
    d->ui.delProfileButton->setIcon( QIcon::fromTheme( QLatin1String( "edit-delete-page" ) ) );

    // connect tablet selector
    connect( d->ui.tabletListSelector,  SIGNAL(currentIndexChanged(QString)), SLOT(onTabletSelectionChanged()) );

    // connect profile selector
    connect( d->ui.addProfileButton, SIGNAL(clicked(bool)), SLOT(addProfile()) );
    connect( d->ui.delProfileButton, SIGNAL(clicked(bool)), SLOT(delProfile()) );
    connect( d->ui.profileSelector,  SIGNAL(currentIndexChanged(QString)), SLOT(switchProfile(QString)) );


    // connect configuration tabs
    connect( &(d->generalPage), SIGNAL(changed()), SLOT(profileChanged()) );
    connect( &(d->stylusPage),  SIGNAL(changed()), SLOT(profileChanged()) );
    connect( &(d->buttonPage),  SIGNAL(changed()), SLOT(profileChanged()) );
    connect( &(d->tabletPage),  SIGNAL(changed()), SLOT(profileChanged()) );
    connect( &(d->touchPage),   SIGNAL(changed()), SLOT(profileChanged()) );

    // connect rotation handling
    connect( &(d->tabletPage),  SIGNAL(rotationChanged(ScreenRotation)), &(d->touchPage), SLOT(onRotationChanged(ScreenRotation)));

    // connect DBus signals
    connect( dbusTabletInterface, SIGNAL(tabletAdded(QString)),   SLOT(onTabletAdded(QString)) );
    connect( dbusTabletInterface, SIGNAL(tabletRemoved(QString)), SLOT(onTabletRemoved(QString)) );
}


void KCMWacomTabletWidget::loadTabletInformation()
{
    Q_D( KCMWacomTabletWidget );
    QDBusReply<QStringList> connectedTablets = DBusTabletInterface::instance().getTabletList();

    if(!connectedTablets.isValid()) {
        return;
    }

    d->ui.tabletListSelector->blockSignals(true);
    foreach(const QString &tabletId, connectedTablets.value()) {
        QDBusReply<QString> deviceName = DBusTabletInterface::instance().getInformation(tabletId, TabletInfo::TabletName.key());
        dbgWacom << "add tablet"<< deviceName << tabletId << "with";
        QDBusReply<QStringList> inputDevices = DBusTabletInterface::instance().getDeviceList(tabletId);
        dbgWacom << inputDevices.value();

        d->ui.tabletListSelector->addItem(QString::fromLatin1("%1 [%2]").arg(deviceName).arg(tabletId),tabletId);
    }
    d->ui.tabletListSelector->blockSignals(false);
}

void KCMWacomTabletWidget::showHideConfig()
{
    // request this to see if dbus works and tablets are connected
    QDBusReply<QStringList> connectedTablets = DBusTabletInterface::instance().getTabletList();

    if( !connectedTablets.isValid() ) {
        QString errorTitle = i18n( "KDE tablet service not found" );
        QString errorMsg   = i18n( "Please start the KDE wacom tablet service to use this configuration dialog.\n"
                                   "The service is required for tablet detection and profile support." );
        showError( errorTitle, errorMsg );
    } else if(!QX11Info::isPlatformX11()) {
        QString errorTitle = i18n("Unsupported platform detected");
        QString errorMsg   = i18n("Currently only X11 is supported.");
        showError(errorTitle, errorMsg);
    } else if( connectedTablets.value().count() == 0 ) {
        QString errorTitle = i18n( "No tablet device detected" );
        QString errorMsg   = i18n( "Please connect a tablet device to continue.\n"
                                   "If your device is already connected, it is currently not in the device database." );
        showError(errorTitle, errorMsg, true);
    } else {
        showConfig();
    }
}

void KCMWacomTabletWidget::onTabletAdded(const QString &tabletId)
{
    Q_D( KCMWacomTabletWidget );

    QDBusReply<QString> deviceName = DBusTabletInterface::instance().getInformation(tabletId, TabletInfo::TabletName.key());
    dbgWacom << "add tablet"<< deviceName << tabletId << "with";
    QDBusReply<QStringList> inputDevices = DBusTabletInterface::instance().getDeviceList(tabletId);
    dbgWacom << inputDevices.value();

    d->ui.tabletListSelector->addItem(QString::fromLatin1("%1 [%2]").arg(deviceName).arg(tabletId),tabletId);
}

void KCMWacomTabletWidget::onTabletRemoved(const QString &tabletId)
{
    Q_D( KCMWacomTabletWidget );

    int index = d->ui.tabletListSelector->findData(tabletId);

    if(index >= 0) {
        d->ui.tabletListSelector->removeItem(index);
    }
}

void KCMWacomTabletWidget::onTabletSelectionChanged()
{
    Q_D( KCMWacomTabletWidget );

    showSaveChanges();

    //tell all widgets to operate on a different tablet now
    QString tabletId = d->ui.tabletListSelector->itemData(d->ui.tabletListSelector->currentIndex()).toString();
    d->generalPage.setTabletId(tabletId);
    d->stylusPage.setTabletId(tabletId);
    d->buttonPage.setTabletId(tabletId);
    d->tabletPage.setTabletId(tabletId);
    d->touchPage.setTabletId(tabletId);

    showHideConfig();
}

void KCMWacomTabletWidget::addProfile()
{
    bool ok;
    QString text = QInputDialog::getText( this,
                                          i18n( "Add new profile" ),
                                          i18n( "Profile name:" ),
                                          QLineEdit::Normal,
                                          QString(), &ok);
    if( !ok || text.isEmpty() ) {
        return;
    }

    ProfileManagement::instance().createNewProfile( text );
    refreshProfileSelector(text);
    switchProfile( text );
}


void KCMWacomTabletWidget::delProfile()
{
    Q_D( KCMWacomTabletWidget );

    ProfileManagement::instance().deleteProfile();
    refreshProfileSelector();
    switchProfile( d->ui.profileSelector->currentText() );

    //update profile rotation selection
    d->generalPage.reloadWidget();
}


void KCMWacomTabletWidget::saveProfile()
{
    Q_D( KCMWacomTabletWidget );

    d->generalPage.saveToProfile();
    d->stylusPage.saveToProfile();
    d->buttonPage.saveToProfile();
    d->tabletPage.saveToProfile();
    d->touchPage.saveToProfile();

    d->profileChanged = false;
    emit changed( false );

    applyProfile();
}


void KCMWacomTabletWidget::switchProfile( const QString &profile )
{
    showSaveChanges();

    ProfileManagement::instance().setProfileName( profile );

    reloadProfile();
    applyProfile();
}


void KCMWacomTabletWidget::reloadProfile()
{
    Q_D( KCMWacomTabletWidget );

    d->generalPage.loadFromProfile();
    d->stylusPage.loadFromProfile();
    d->buttonPage.loadFromProfile();
    d->tabletPage.loadFromProfile();
    d->touchPage.loadFromProfile();

    d->profileChanged = false;
    emit changed( false );
}


void KCMWacomTabletWidget::applyProfile()
{
    Q_D( KCMWacomTabletWidget );

    QString tabletId = d->ui.tabletListSelector->itemData(d->ui.tabletListSelector->currentIndex()).toString();
    DBusTabletInterface::instance().setProfile( tabletId, ProfileManagement::instance().profileName() );
}


void KCMWacomTabletWidget::profileChanged()
{
    Q_D( KCMWacomTabletWidget );

    d->profileChanged = true;
    emit changed( true );
}


void KCMWacomTabletWidget::showError(const QString& errorTitle, const QString &errorMsg, bool showTabletFinderButton)
{
    Q_D( KCMWacomTabletWidget );

    hideError();
    hideConfig();

    d->deviceErrorUi.errorTitle->setText(errorTitle);
    d->deviceErrorUi.errorText->setText (errorMsg);
    d->ui.verticalLayout->addWidget (&(d->deviceErrorWidget));
    d->deviceErrorWidget.setVisible(true);
    d->deviceErrorUi.buttonRunTabletFinder->setVisible(showTabletFinderButton);
}


void KCMWacomTabletWidget::hideConfig()
{
    Q_D( KCMWacomTabletWidget );

    d->ui.tabletListSelector->setVisible( false );
    d->ui.tabletListLabel->setVisible( false );
    d->ui.profileSelector->setVisible( false );
    d->ui.profileLabel->setVisible( false );
    d->ui.addProfileButton->setVisible( false );
    d->ui.delProfileButton->setVisible( false );

    d->ui.tabletListSelector->setEnabled( false );
    d->ui.profileSelector->setEnabled( false );
    d->ui.addProfileButton->setEnabled( false );
    d->ui.delProfileButton->setEnabled( false );
    d->ui.deviceTabWidget->setVisible( false );
}


void KCMWacomTabletWidget::hideError()
{
    Q_D( KCMWacomTabletWidget );

    d->deviceErrorWidget.setVisible(false);
    d->ui.verticalLayout->removeWidget (&(d->deviceErrorWidget));
}


bool KCMWacomTabletWidget::refreshProfileSelector ( const QString& profile )
{
    Q_D( KCMWacomTabletWidget );

    int         index    = -1;
    QStringList profiles = ProfileManagement::instance().availableProfiles();

    d->ui.profileSelector->blockSignals( true );
    d->ui.profileSelector->clear();
    d->ui.profileSelector->addItems( profiles );

    if (!profile.isEmpty()) {
        index = d->ui.profileSelector->findText( profile );
        d->ui.profileSelector->setCurrentIndex( index );

    } else if (!profiles.isEmpty()) {
        index = 0;
        d->ui.profileSelector->setCurrentIndex( index );
    }

    d->ui.profileSelector->blockSignals( false );

    return (index >= 0);
}


void KCMWacomTabletWidget::showConfig()
{
    Q_D( KCMWacomTabletWidget );

    // make sure no error message is active
    hideError();

    // reload profile and widget data
    QString tabletId = d->ui.tabletListSelector->itemData(d->ui.tabletListSelector->currentIndex()).toString();
    ProfileManagement::instance().setTabletId(tabletId);
    ProfileManagement::instance().reload();

    d->generalPage.setTabletId(tabletId);
    d->stylusPage.setTabletId(tabletId);
    d->buttonPage.setTabletId(tabletId);
    d->tabletPage.setTabletId(tabletId);
    d->touchPage.setTabletId(tabletId);

    d->generalPage.reloadWidget();
    d->stylusPage.reloadWidget();
    d->buttonPage.reloadWidget();
    d->tabletPage.reloadWidget();
    d->touchPage.reloadWidget();

    //show tablet Selector
    d->ui.tabletListSelector->setEnabled( true );
    d->ui.tabletListLabel->setVisible( true );
    d->ui.tabletListSelector->setVisible( true );

    // initialize profile selector
    d->ui.profileSelector->setEnabled( true );
    d->ui.addProfileButton->setEnabled( true );
    d->ui.delProfileButton->setEnabled( true );
    d->ui.profileLabel->setVisible( true );
    d->ui.profileSelector->setVisible( true );
    d->ui.addProfileButton->setVisible( true );
    d->ui.delProfileButton->setVisible( true );

    if( ProfileManagement::instance().availableProfiles().isEmpty() ) {
        ProfileManagement::instance().createNewProfile(i18nc( "Name of the default profile that will be created if none exist.","Default" ));
        applyProfile();
    }

    refreshProfileSelector();


    // initialize configuration tabs
    d->ui.deviceTabWidget->clear();
    d->ui.deviceTabWidget->addTab( &(d->generalPage), i18nc( "Basic overview page for the tablet hardware", "General" ) );
    d->ui.deviceTabWidget->addTab( &(d->stylusPage), i18n( "Stylus" ) );


    QDBusReply<bool> hasPadButtons = DBusTabletInterface::instance().hasPadButtons(tabletId);

    if( hasPadButtons.isValid() && hasPadButtons.value() ) {
        d->ui.deviceTabWidget->addTab( &(d->buttonPage), i18n( "Express Buttons" ) );
    }

    d->ui.deviceTabWidget->addTab( &(d->tabletPage), i18n ("Tablet") );

    QDBusReply<QString> touchDeviceName = DBusTabletInterface::instance().getDeviceName(tabletId, DeviceType::Touch.key());
    bool                hasTouchDevice  = (touchDeviceName.isValid() && !touchDeviceName.value().isEmpty());

    if (hasTouchDevice) {
        d->ui.deviceTabWidget->addTab( &(d->touchPage), i18n ("Touch") );
    }

    d->ui.deviceTabWidget->setEnabled( true );
    d->ui.deviceTabWidget->setVisible( true );

    // switch to the currently active profile
    QDBusReply<QString> profile = DBusTabletInterface::instance().getProfile(tabletId);
    if( profile.isValid() ) {
        d->ui.profileSelector->setCurrentText( profile );
        switchProfile( profile );
    }
}


void KCMWacomTabletWidget::showSaveChanges()
{
    Q_D( KCMWacomTabletWidget );

    if( d->profileChanged ) {
        QPointer<QDialog> saveDialog = new QDialog();
        QWidget*          widget     = new QWidget( this );

        Ui::SaveProfile askToSave;
        askToSave.setupUi( widget );

        QVBoxLayout* layout = new QVBoxLayout;
        QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
        layout->addWidget( widget );
        layout->addWidget( buttonBox);

        connect( buttonBox, &QDialogButtonBox::clicked, [saveDialog, buttonBox](QAbstractButton* button) {
            auto standardButton = buttonBox->standardButton(button);
            if (standardButton == QDialogButtonBox::Apply) {
                saveDialog->accept();
            } else { // Cancel
                saveDialog->reject();
            }
        });

        if( saveDialog->exec() == QDialog::Accepted ) {
            saveProfile();
        }

        delete saveDialog;
    }
}

void KCMWacomTabletWidget::showTabletFinder()
{
    bool success = QProcess::startDetached(QStringLiteral("kde_wacom_tabletfinder"));

    if (!success) {
        QString err = i18n("Failed to launch Wacom tablet finder tool. Check your installation.");
        QMessageBox::warning(QApplication::activeWindow(), QApplication::applicationName(), err);
    }
}
