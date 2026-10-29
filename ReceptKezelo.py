import json
import os

RECEPT_FAJL = "receptek.json"


def receptek_betoltese():
    """Beolvassa a receptek.json fájlt, vagy üres szótárral tér vissza."""
    if not os.path.exists(RECEPT_FAJL):
        return {}
    try:
        with open(RECEPT_FAJL, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception as e:

        print(f"[HIBA] Nem sikerült beolvasni a receptfájlt: {e}")
        return {}


def receptek_mentese(adatbázis):
    """Elmenti az adatbázist a receptek.json fájlba, szépen formázva."""
    try:
        with open(RECEPT_FAJL, "w", encoding="utf-8") as f:
            json.dump(adatbázis, f, ensure_ascii=False, indent=4)
        print(f"-> Recept sikeresen elmentve a '{RECEPT_FAJL}' fájlba!")
    except Exception as e:
        print(f"[HIBA] Nem sikerült elmenteni a receptet: {e}")


def pozitiv_szam_bekerese(szoveg, tipus=float):
    """Segédfüggvény érvényes szám bekéréséhez."""
    while True:
        nyers = input(szoveg).strip().replace(",", ".")
        try:
            ertek = tipus(nyers)
            if ertek < 0:
                print("  [!] Az érték nem lehet negatív!")
                continue
            return ertek
        except ValueError:
            print("  [!] Kérlek érvényes számot adj meg!")


def uj_recept_bekerese(noveny_nev):
    """
    Bekéri a struct NovenyFejlodesFazis-nak megfelelő adatokat
    1-től maximum 3 fázisra.
    """
    print(f"\n--- Új recept felvétele: {noveny_nev} ---")

    while True:
        fazis_db = input("Hány fázisból álljon a termesztés? (1-3): ").strip()
        if fazis_db in ['1', '2', '3']:
            fazis_db = int(fazis_db)
            break
        print("  [!] Kérlek 1, 2 vagy 3 fázist válassz!")

    fazisok = []
    for i in range(1, fazis_db + 1):
        print(f"\n>> {i}. FÁZIS ADATAI <<")
        f_nev = input(f"  Fázis megnevezése (pl. Csirazas / Novekedes / Gyumolcs): ").strip()
        if not f_nev:
            f_nev = f"{i}. fazis"

        idotartam_nap = pozitiv_szam_bekerese("  Fázis időtartama [nap]: ", int)
        cel_lux = pozitiv_szam_bekerese("  Cél megvilágítás (plató) [Lux]: ", float)
        cel_rb = pozitiv_szam_bekerese("  Cél R/B arány [-] (pl. 1.8): ", float)


        nappal_hossz_s = pozitiv_szam_bekerese("  Nappali fényidőszak hossza [másodperc] (pl. 57600 = 16 óra): ", int)
        ejszaka_hossz_s = pozitiv_szam_bekerese("  Éjszakai sötét időszak hossza [másodperc] (pl. 28800 = 8 óra): ",
                                                int)
        pirkadat_hossz_s = pozitiv_szam_bekerese(
            "  Pirkadat/szürkület átmenet hossza [másodperc] (pl. 1800 = 30 perc): ", int)


        fazis_adat = {
            "nev": f_nev,
            "idotartamNap": idotartam_nap,
            "celLux": cel_lux,
            "celRB": cel_rb,
            "nappalHossz_s": nappal_hossz_s,
            "ejszakaHossz_s": ejszaka_hossz_s,
            "pirkadatHossz_s": pirkadat_hossz_s
        }
        fazisok.append(fazis_adat)

    return fazisok


def recept_megjelenitese(noveny_nev, fazisok):
    """Áttekinthető táblázat formájában kiírja a recept fázisait."""
    print(f"\n=======================================================")
    print(f"             RECEPT: {noveny_nev.upper()}")
    print(f"=======================================================")
    for idx, f in enumerate(fazisok, 1):
        print(f"[{idx}. Fázis: {f['nev']}]")
        print(f"  - Időtartam:         {f['idotartamNap']} nap")
        print(f"  - Célértékek:        {f['celLux']:.1f} Lux | R/B: {f['celRB']:.2f}")
        print(f"  - Fény/Sötét ciklus: {f['nappalHossz_s']}s nappal / {f['ejszakaHossz_s']}s éjszaka")
        print(f"  - Átmeneti idő:      {f['pirkadatHossz_s']}s (pirkadat/szürkület)")
        print("-" * 55)


def noveny_kivalasztasa_vagy_letrehozasa():
    """
    Fő kezelőfüggvény:
    1. Bekéri a növény nevét
    2. Ha van elmentve: felkínálja ellenőrzésre
    3. Ha nincs vagy újat akar: bekéri és elmenti
    """
    receptek = receptek_betoltese()

    if receptek:
        print("\nEddig elmentett receptek:", ", ".join(receptek.keys()))

    noveny = input("\nMilyen növényt termesztesz? (pl. Paradicsom): ").strip().capitalize()
    if not noveny:
        print("[!] Név megadása kötelező.")
        return None

    if noveny in receptek:
        print(f"\n-> Találtunk elmentett receptet a(z) '{noveny}' növényhez!")
        recept_megjelenitese(noveny, receptek[noveny])

        hasznal = input("Használjuk ezt a receptet? (I/N, Enter = I): ").strip().lower()
        if hasznal in ['', 'i', 'igen', 'y', 'yes']:
            return {noveny: receptek[noveny]}
        else:
            ujrair = input("Szeretnéd felülírni ezt a receptet új adatokkal? (I/N): ").strip().lower()
            if ujrair not in ['i', 'igen', 'y', 'yes']:
                print("Receptválasztás megszakítva.")
                return None


    uj_fazisok = uj_recept_bekerese(noveny)
    recept_megjelenitese(noveny, uj_fazisok)

    mentes = input("Elmentsük ezt a receptet? (I/N, Enter = I): ").strip().lower()
    if mentes in ['', 'i', 'igen', 'y', 'yes']:
        receptek[noveny] = uj_fazisok
        receptek_mentese(receptek)

    return {noveny: uj_fazisok}


if __name__ == "__main__":
    kivalasztott = noveny_kivalasztasa_vagy_letrehozasa()
    if kivalasztott:
        print("\nSikeresen kiválasztott termesztési profil:")
        print(list(kivalasztott.keys())[0])