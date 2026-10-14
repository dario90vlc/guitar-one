' Lanzador de GuitarAmp: doble clic aqui.
'  - No abre ninguna consola.
'  - Compila solo si GuitarAmpNative.cpp cambio.
'  - Abre el .exe con CreateProcess (sin el aviso de "editor desconocido").
Set sh  = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
root = fso.GetParentFolderName(WScript.ScriptFullName)
cmd = "powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File """ & root & "\_launch.ps1"""
sh.Run cmd, 0, False
