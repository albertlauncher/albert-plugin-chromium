cmake_minimum_required(VERSION 3.16)
project(chromium VERSION 9.3.2)

find_package(Albert REQUIRED)

albert_plugin(QT Widgets Concurrent Sql)
