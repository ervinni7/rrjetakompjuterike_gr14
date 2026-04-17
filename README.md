# rrjetakompjuterike_gr14
# Projekti 2 - Rrjetat Kompjuterike
## Grupi 14 | TCP | Gjuha C

---

## Përshkrimi
Ky projekt implementon një **TCP server-klient** sistem në gjuhën **C** me këto funksionalitete:

### Serveri (`server.c`)
- Dëgjon në portin **5000** (TCP) dhe **8080** (HTTP)
- Pranon deri në **4 klientë** njëkohësisht
- Refuzon lidhjet kur serveri është plot
- Klienti i parë merr **privilegje admin** (write/read/execute)
- Klientët tjerë kanë vetëm **read** permission
- Timeout: mbyll lidhjen nëse klienti nuk dërgon mesazh brenda **60 sekondave**
- Rikuperon automatikisht lidhjen nëse klienti rifutet
- Ruan mesazhet e klientëve për monitorim
- HTTP endpoint `GET /stats` kthen statistikat në format JSON
- Admin ka kohë përgjigjeje më të shpejtë (prioritet)

### Klienti (`client.c`)
- Lidhet me serverin duke specifikuar IP dhe portin
- Dërgon mesazhe tekst dhe lexon përgjigjet
- Klienti admin ka qasje të plotë në komandat e file-ave



## Ekzekutimi

### Hapi 1: Starto serverin
bash
./server.exe
Serveri do të dëgjojë në portin 5000 (TCP) dhe 8080 (HTTP).

### Hapi 2: Lidhu me klient (nga pajisje të ndryshme në rrjet)
```bash
# Klienti i parë (do të jetë ADMIN):
./client 192.168.1.100 12345

# Klientët tjerë (READ-ONLY):
./client 192.168.1.100 12345
```

**Shënim:** Zëvendëso `192.168.1.100` me IP-në reale të serverit.
Për të gjetur IP-në: `ip addr show` ose `ifconfig`

### Hapi 3: Monitoro me HTTP
# Nga browseri ose curl:
curl http://192.168.1.100:8080/stats

## Testimi me 4 pajisje

1. Sigurohuni që të gjitha pajisjet janë në **të njëjtin rrjet** (WiFi ose LAN)
2. Instaloni **gcc** në secilën pajisje
3. Startoni serverin në një pajisje
4. Lidhni 4 klientë nga pajisje të ndryshme
5. Testoni komandat dhe mesazhet
6. Kontrolloni HTTP stats: `http://<server-ip>:8080/stats`

## Anëtarët e grupit
- Ernesa Mavraj
- Ermira Zeka
- Euron Ademaj
- Ervin Nimani


