# Paradoks Monty Halla – symulator z grafiką (C++ / raylib)

Interaktywna aplikacja, w której **sam grasz w Monty Halla** albo uruchamiasz
symulację milionów gier i sprawdzasz, czy opłaca się zmienić drzwi.
Wbudowane **animowane wyjaśnienie paradoksu** krok po kroku.

![Wyjaśnienie – krok 4](docs/wyjasnienie-krok4.png)

## Czym jest paradoks Monty Halla?

Za jednymi z trzech drzwi stoi auto, za dwoma pozostałymi – kozy.
Wybierasz drzwi. Prowadzący (który wie, gdzie jest auto) otwiera inne drzwi
z kozą i pyta: *zostajesz czy zmieniasz?*

**Zmiana wygrywa w 2/3 przypadków, zostanie przy wyborze tylko w 1/3.**

## Funkcje

- tryb ręczny (domyślny): klikasz drzwi, potem decydujesz ZOSTAJĘ / ZMIENIAM
- tryb losowy z auto-grą i opcjonalną podpowiedzią, gdzie jest auto
- szybka symulacja bez animacji (do 100 000 000 gier) z wykresem zbieżności do teorii
- statystyki, historia gier, zapis wyników do pliku `wyniki_monty_hall.txt`
- przycisk **WYJAŚNIENIE PARADOKSU** – 7 animowanych slajdów (w tym wersja „100 drzwi”)
- scena teatralna, animacje, konfetti, okno skalowalne
- czcionki (Poppins, Bungee) wbudowane w kod, brak zewnętrznych zasobów

![Wyjaśnienie – krok 5](docs/wyjasnienie-krok5.png)

## Wymagania

- Windows 10/11 + [MSYS2](https://www.msys2.org/) (powłoka **UCRT64**)
- kompilator g++ (C++17) i biblioteka raylib (5.x)

## Instalacja i kompilacja (MSYS2 UCRT64)

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-raylib

g++ -std=c++17 -O2 monty_hall_gui.cpp -o monty_hall_gui.exe \
    -lraylib -lglfw3 -lopengl32 -lgdi32 -lwinmm -mwindows
```

Przy uruchamianiu poza MSYS2 skopiuj obok pliku `.exe` biblioteki DLL
(np. `libraylib.dll`, `libglfw3.dll`, `libgcc_s_seh-1.dll`, `libstdc++-6.dll`,
`libwinpthread-1.dll`) z katalogu `C:\msys64\ucrt64\bin`.

## Sterowanie

| Akcja | Jak |
|---|---|
| Nowa gra | przycisk NOWA GRA lub Spacja |
| Wybór drzwi | klik w drzwi |
| Decyzja | ZOSTAJĘ / ZMIENIAM |
| Szybka symulacja | wpisz liczbę gier (lub preset) i SYMULUJ |
| Wyjaśnienie | WYJAŚNIENIE PARADOKSU (←/→ zmiana kroku, Esc zamyka) |

## Licencja

MIT – zobacz plik [LICENSE](LICENSE).
