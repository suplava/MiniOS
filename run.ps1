gcc -Iinclude kernel/*.c -o minios.exe -Wall -Wextra
if ($?) { ./minios.exe }
