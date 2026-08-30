# iDryerMod

Модульный каркас прошивки двухкатушечной сушилки на ESP32-WROOM-32D.

## Сборка

~~~powershell
python -m platformio run -e esp32dev
python -m platformio run -e esp32dev -t buildfs
python -m platformio run -e esp32dev -t upload
python -m platformio run -e esp32dev -t uploadfs
~~~

После первой загрузки без сохранённой сети ESP32 создаёт точку доступа
FilamentDryer-Setup. В локальной сети устройство публикуется как
filament-dryer.local.

Веб-интерфейс и REST API используют HTTP Basic Auth:

- пользователь: admin;
- начальный пароль: change-me.

Перед подключением силовой части обязательно смените OTA/API-пароль и проверьте
безопасное выключение GPIO26 при старте, сбросе, ошибке NTC и OTA.

GPIO и аппаратные параметры находятся в include/config/BoardConfig.h, настройки по
умолчанию — в include/config/Defaults.h. Архитектурные решения описаны в
docs/ARCHITECTURE.md.
