@echo off
"%~dp0ModuleLoader.exe" --stop-local-releases
if errorlevel 1 goto erreur
echo Le jeu doit continuer a executer ses frames.
echo Le journal module.log de la revision chargee doit afficher STOPPED puis HOOKS_DISABLED.
echo La DLL reste inactive en memoire jusqu'a la fermeture normale du jeu.
echo Redemarrez le jeu vous-meme avant de recharger ce module.
goto fin
:erreur
echo La demande d'arret n'a pas pu etre confirmee pour toutes les revisions.
echo Consultez le message ci-dessus et le journal de la revision chargee.
:fin
pause
