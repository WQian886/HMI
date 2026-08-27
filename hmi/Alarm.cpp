// 阶段 5.2(2026-08-04):AlarmChecker 基类实现
//   getter/setter + 信号由 moc 处理
//   5.3 阶段才加 HighAlarmChecker / LowAlarmChecker 子类

#include "Alarm.h"

AlarmChecker::AlarmChecker(QObject* parent) : QObject(parent) {}
AlarmChecker::~AlarmChecker() = default;

QString AlarmChecker::name() const { return name_; }
void    AlarmChecker::setName(const QString& n) { name_ = n; }
AlarmLevel AlarmChecker::level() const { return level_; }
void    AlarmChecker::setLevel(AlarmLevel l) { level_ = l; }