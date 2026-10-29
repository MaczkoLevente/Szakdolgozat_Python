
import requests
import json

ESP_IP = "192.168.4.1"
ESP_URL = f"http://{ESP_IP}/beallitas"
TIMEOUT_MP = 5  # Másodpercben mért időkorlát


def fenyrecept_feltoltese(noveny_neve: str, fazisok_lista: list) -> bool:
    informacio = {
        "noveny": noveny_neve,
        "fazisok": fazisok_lista
    }

    try:
        valasz = requests.post(
            ESP_URL,
            json=informacio,
            headers={"Content-Type": "application/json"},
            timeout=TIMEOUT_MP,
        )

        if valasz.status_code == 200:
            print("-> Sikeres adatátvitel! Az ESP32 nyugtázta a beállítást.")
            return True
        else:
            print(f"-> Az ESP32 hibát jelzett: HTTP {valasz.status_code}")
            print(f"-> Válasz: {valasz.text}")
            return False

    except requests.exceptions.ConnectTimeout:
        print("-> Időtúllépés: Az ESP32 nem válaszol. Csatlakoztál a Wi-Fi-jére?")
        return False
    except requests.exceptions.ConnectionError:
        print("-> Kapcsolódási hiba: Nem érhető el a 192.168.4.1 cím.")
        return False
    except Exception as e:
        print(f"-> Nem várt hiba történt a küldés során: {e}")
        return False


if __name__ == "__main__":

    teszt_fazisok = [
        {
            "nev": "Teszt_Vegetativ",
            "idotartamNap": 14,
            "celLux": 15000,
            "celRB": 2.1,
            "nappalHossz_s": 57600,
            "ejszakaHossz_s": 25200,
            "pirkadatHossz_s": 3600
        }
    ]
    print("Teszt küldés indítása...")
    fenyrecept_feltoltese(teszt_fazisok)