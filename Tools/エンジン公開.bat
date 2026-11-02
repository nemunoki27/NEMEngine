@echo off
setlocal
node "%~dp0Distribution\publish.mjs" %*
exit /b %errorlevel%
