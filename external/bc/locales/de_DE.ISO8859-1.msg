$quote "

$ Headers for printing errors/warnings.
$set 1

1 "Funktion:"

$ Error types.
$set 2

1 "Rechenfehler:"
2 "Analysefehler:"
3 "Laufzeitfehler:"
4 "Fataler Fehler:"
5 "Warnung:"

$ Math errors.
$set 3

1 "negative Zahl"
2 "Nicht-Ganzzahl-Wert"
3 "?berlauf: Zahl passt nicht in Register"
4 "Division durch 0"

$ Parse errors.
$set 4

1 "Ende der Datei"
2 "ung?ltiges Zeichen: '%c'"
3 "Zeichenketten-Ende konnte nicht gefunden werden"
4 "Kommentar-Ende konnte nicht gefunden werden"
5 "ung?ltiges Token"
6 "ung?ltiger Ausdruck"
7 "leerer Ausdruck"
8 "Ung?ltige Druckanweisung"
9 "Ung?ltige Funktionsdefinition"
10 "Ung?ltige Zuweisung: Die linke Seite muss \"scale\", \"ibase\", \"obase\", \"last\", \"var\" oder \"array element\" sein"
11 "keine automatische Variable gefunden"
12 "Funktionsparameter oder Variable \"%s%s\" existiert bereits"
13 "Blockende konnte nicht gefunden werden"
14 "eine \"void-Funktion\" kann keinen Wert zur?ckgeben: %s()"
15 "Variable kann keine Referenz sein: %s"
16 "POSIX erlaubt keine Namen mit mehr als 1 Zeichen L?nge: %s"
17 "POSIX erlaubt keine '#'-Skriptkommentare"
18 "POSIX erlaubt das Schl?sselwort \"%s\" nicht"
19 "POSIX erlaubt keinen Punkt ('.') als Abk?rzung f?r das letzte Ergebnis"
20 "POSIX ben?tigt Klammern um R?ckgabeausdr?cke"
21 "POSIX erlaubt den Operator \"%s\" nicht"
22 "POSIX erlaubt keine Vergleichsoperatoren au?erhalb von if-Anweisungen oder Schleifen"
23 "POSIX ben?tigt 0 oder 1 Vergleichsoperatoren pro Bedingung"
24 "POSIX erlaubt keinen leeren Ausdruck in einer for-Schleife"
25 "POSIX erlaubt keine exponentielle Notation"
26 "POSIX erlaubt keine Feld-Referenzen als Funktionsparameter"
27 "POSIX erfordert, dass die linke Klammer auf der gleichen Linie wie der Funktionskopf steht"

$ Runtime errors.
$set 5

1 "ung?ltige \"ibase\": muss im Intervall [%lu, %lu] liegen"
2 "ung?ltige \"obase\": muss im Intervall [%lu, %lu] liegen"
3 "ung?ltige \"scale\": muss im Intervall [%lu, %lu] liegen"
4 "ung?ltiger read()-Ausdruck"
5 "rekursiver read()-Aufruf"
6 "Variable oder Feld-Element hat den falschen Typ"
7 "Stapel hat zu wenig Elemente"
8 "falsche Anzahl der Parameter: ben?tigt %zu, hat %zu"
9 "undefinierte Funktion: %s()"
10 "kann keinen ung?ltigen Wert in einem Ausdruck verwenden"

$ Fatal errors.
$set 6

1 "Speicherzuweisung fehlgeschlagen"
2 "Ein-Ausgabe-Fehler"
3 "konnte die Datei nicht ?ffnen: %s"
4 "Datei ist nicht ASCII: %s"
5 "Pfad ist ein Verzeichnis: %s"
6 "ung?ltige Befehlszeilenoption: '%c' (\"%s\")"
