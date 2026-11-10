/****************************************************************************
** Meta object code from reading C++ file 'settingsdialog.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../ui/settingsdialog.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'settingsdialog.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.8.3. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN14SettingsDialogE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN14SettingsDialogE = QtMocHelpers::stringData(
    "SettingsDialog",
    "databaseRestored",
    "",
    "serverAddressChanged",
    "roleChanged",
    "onFieldCatChanged",
    "index",
    "onAddField",
    "onDeleteField",
    "onAddUser",
    "onDeleteUser",
    "onResetPwd",
    "onPickCsv",
    "onImport",
    "refreshOffline",
    "onViewOpLog",
    "onDeleteOpLog",
    "onClearOpLogs",
    "onPickWorkRoot",
    "onConfirmWorkRoot",
    "onCancelWorkRoot",
    "onBackupNow",
    "onRestore",
    "onRestoreBackup",
    "onDeleteBackup",
    "onAutoBackupToggled",
    "on",
    "onAutoBackupTimeChanged",
    "checkAutoBackup",
    "refreshUpdateInfo",
    "onPickUpdatePackage",
    "onUploadUpdatePackage",
    "onPickAndroidPackage",
    "onUploadAndroidPackage"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN14SettingsDialogE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      30,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,  194,    2, 0x06,    1 /* Public */,
       3,    0,  195,    2, 0x06,    2 /* Public */,
       4,    0,  196,    2, 0x06,    3 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       5,    1,  197,    2, 0x08,    4 /* Private */,
       7,    0,  200,    2, 0x08,    6 /* Private */,
       8,    0,  201,    2, 0x08,    7 /* Private */,
       9,    0,  202,    2, 0x08,    8 /* Private */,
      10,    0,  203,    2, 0x08,    9 /* Private */,
      11,    0,  204,    2, 0x08,   10 /* Private */,
      12,    0,  205,    2, 0x08,   11 /* Private */,
      13,    0,  206,    2, 0x08,   12 /* Private */,
      14,    0,  207,    2, 0x08,   13 /* Private */,
      15,    0,  208,    2, 0x08,   14 /* Private */,
      16,    0,  209,    2, 0x08,   15 /* Private */,
      17,    0,  210,    2, 0x08,   16 /* Private */,
      18,    0,  211,    2, 0x08,   17 /* Private */,
      19,    0,  212,    2, 0x08,   18 /* Private */,
      20,    0,  213,    2, 0x08,   19 /* Private */,
      21,    0,  214,    2, 0x08,   20 /* Private */,
      22,    0,  215,    2, 0x08,   21 /* Private */,
      23,    0,  216,    2, 0x08,   22 /* Private */,
      24,    0,  217,    2, 0x08,   23 /* Private */,
      25,    1,  218,    2, 0x08,   24 /* Private */,
      27,    0,  221,    2, 0x08,   26 /* Private */,
      28,    0,  222,    2, 0x08,   27 /* Private */,
      29,    0,  223,    2, 0x08,   28 /* Private */,
      30,    0,  224,    2, 0x08,   29 /* Private */,
      31,    0,  225,    2, 0x08,   30 /* Private */,
      32,    0,  226,    2, 0x08,   31 /* Private */,
      33,    0,  227,    2, 0x08,   32 /* Private */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void, QMetaType::Int,    6,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,   26,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject SettingsDialog::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_ZN14SettingsDialogE.offsetsAndSizes,
    qt_meta_data_ZN14SettingsDialogE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN14SettingsDialogE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<SettingsDialog, std::true_type>,
        // method 'databaseRestored'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'serverAddressChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'roleChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onFieldCatChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onAddField'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onDeleteField'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onAddUser'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onDeleteUser'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onResetPwd'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onPickCsv'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onImport'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'refreshOffline'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onViewOpLog'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onDeleteOpLog'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onClearOpLogs'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onPickWorkRoot'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onConfirmWorkRoot'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onCancelWorkRoot'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onBackupNow'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onRestore'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onRestoreBackup'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onDeleteBackup'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onAutoBackupToggled'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'onAutoBackupTimeChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'checkAutoBackup'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'refreshUpdateInfo'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onPickUpdatePackage'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onUploadUpdatePackage'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onPickAndroidPackage'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onUploadAndroidPackage'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void SettingsDialog::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<SettingsDialog *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->databaseRestored(); break;
        case 1: _t->serverAddressChanged(); break;
        case 2: _t->roleChanged(); break;
        case 3: _t->onFieldCatChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 4: _t->onAddField(); break;
        case 5: _t->onDeleteField(); break;
        case 6: _t->onAddUser(); break;
        case 7: _t->onDeleteUser(); break;
        case 8: _t->onResetPwd(); break;
        case 9: _t->onPickCsv(); break;
        case 10: _t->onImport(); break;
        case 11: _t->refreshOffline(); break;
        case 12: _t->onViewOpLog(); break;
        case 13: _t->onDeleteOpLog(); break;
        case 14: _t->onClearOpLogs(); break;
        case 15: _t->onPickWorkRoot(); break;
        case 16: _t->onConfirmWorkRoot(); break;
        case 17: _t->onCancelWorkRoot(); break;
        case 18: _t->onBackupNow(); break;
        case 19: _t->onRestore(); break;
        case 20: _t->onRestoreBackup(); break;
        case 21: _t->onDeleteBackup(); break;
        case 22: _t->onAutoBackupToggled((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 23: _t->onAutoBackupTimeChanged(); break;
        case 24: _t->checkAutoBackup(); break;
        case 25: _t->refreshUpdateInfo(); break;
        case 26: _t->onPickUpdatePackage(); break;
        case 27: _t->onUploadUpdatePackage(); break;
        case 28: _t->onPickAndroidPackage(); break;
        case 29: _t->onUploadAndroidPackage(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (SettingsDialog::*)();
            if (_q_method_type _q_method = &SettingsDialog::databaseRestored; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (SettingsDialog::*)();
            if (_q_method_type _q_method = &SettingsDialog::serverAddressChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (SettingsDialog::*)();
            if (_q_method_type _q_method = &SettingsDialog::roleChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
    }
}

const QMetaObject *SettingsDialog::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SettingsDialog::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN14SettingsDialogE.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int SettingsDialog::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 30)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 30;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 30)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 30;
    }
    return _id;
}

// SIGNAL 0
void SettingsDialog::databaseRestored()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void SettingsDialog::serverAddressChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void SettingsDialog::roleChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}
QT_WARNING_POP
