# AstraScript Başvuru Kılavuzu (AstraSageOS 0.9.3)

Kaynak: `sys/astrascript.c` (~3000 satır). Satır tabanlı yorumlayıcı. Dosya uzantısı `.as`, çalıştırma: `as asscript dosya.as` (script içinden başka script çalıştırılamaz).

## 1. Yapı taşları (blok / anahtar sözcük)

| Yapı | Sözdizimi | Not |
|---|---|---|
| Başlangıç/bitiş | `start;` ... `end;` | Yapısal işaretler |
| Blok | `{ }` | Her `{` ve `}` kendi satırında olabilir |
| Yorum | `@ yorum` (satır başı) ve `@** ... **@` (blok) | |
| Değişken | `vr inter x = 5;` `vr flo f = 1.5;` `vr strt s = "metin";` | Bir `vr` = tek değişken; ilk değer isteğe bağlı |
| Atama | `x = x + 1;` `s = "a";` `s = t;` | Metinde `+` yok → `str_cat` |
| Yazdır | `printc(a, b, "metin");` | Argümanlar boşlukla ayrılır, sonda satır sonu |
| Girdi | `inputc(x);` | inter/flo/strt okur |
| Koşul | `if (koşul) { }` / `else if (koşul) { }` / `else { }` | `else` satır başında olmalı; `} else {` YOK |
| Döngü | `whle (koşul) { }` | |
| Sayaçlı döngü | `fr (i = 0; i < 10; i = i + 1) { }` | Artırma bir atama olmalı (`i++` yok) |
| Fonksiyon | `func ad() { }` çağrı: `ad();` | Parametre ve dönüş değeri YOK, değişkenler global |
| Paket kullanımı | `use PaketAdı;` | Kurulu ASPI paketinin `.as` dosyalarını dahil eder |
| Terminal komutu | `as <komut>;` | `asscript`, `aspi` engelli |

## 2. Türler

- `inter` – tamsayı
- `flo` – double (6 anlamlı basamakla yazdırılır)
- `strt` – metin, en çok 255 karakter

## 3. İfadeler ve koşullar

- Aritmetik: `+ - * / %`, parantez, tekli `+`/`-`, sayılar, değişkenler.
- Karşılaştırma: `== != >= <= > <` (metinlerde de çalışır).
- Mantık: `&&  ||  !`, parantez, `true` / `false`.
- Koşul en az bir karşılaştırma içermeli, yoksa "Invalid condition."

## 4. Yerleşik komutlar (0.9.3)

### Pencere / çizim
| Komut | Anlam |
|---|---|
| `gui_open(başlık, g, y)` | Pencere aç (en az 360x220 zorlanır) |
| `gui_close` | Kapat |
| `gui_clear(renk)` | Tuvali boya |
| `gui_rect(x, y, g, y, renk)` | Dolu dikdörtgen |
| `gui_pixel(x, y, renk)` | Nokta |
| `gui_text(x, y, metin, renk[, ölçek])` | Yazı |
| `gui_pal(i, renk)` | Palet girdisi (sprite için) |
| `gui_sprite(x, y, ölçek, "satır", ...)` | `.`=saydam, `0-9 a-f`=palet; negatif ölçek=aynalı |
| `gui_show` | Çizimi ekrana yansıt |
| `gui_wait(değ, ms)` | Tuş kodu, `1000`=tıklama, `0`=zaman aşımı, `-1`=pencere kapandı |
| `gui_mouse(xdeğ, ydeğ)` | Fare konumu |

Renk: `"#RRGGBB"` veya sayı.

### Metin
| Komut | Anlam |
|---|---|
| `str_set(d, metin)` | d = metin |
| `str_cat(d, a, b)` | d = a + b |
| `str_add(d, kod)` | d sonuna ASCII karakter (32–126) |
| `str_del(d)` | Son karakteri sil |
| `str_low(d, metin)` | Küçük harf |
| `str_word(d, metin, n)` | n. kelime |
| `str_rest(d, metin, n)` | n. kelimeden sonuna |
| `str_sub(d, metin, başla, adet)` | Alt metin (başla 1'den) |
| `str_line(d, metin, n)` | n. satır |
| `str_len(sonuç, metin)` | Uzunluk |
| `str_num(sonuç, metin)` | Metinden sayı |
| `str_has(sonuç, metin, "a\|b\|c")` | İçeriyor mu (büyük/küçük harf fark etmez) → 1/0 |
| `str_is(sonuç, metin, "a\|b\|c")` | Tamamen eşit mi → 1/0 |

### Dosya ve komut
| Komut | Anlam |
|---|---|
| `file_put(yol, metin)` | Dosyaya yaz (üzerine) |
| `file_add(yol, metin)` | Dosyaya ekle |
| `file_has(sonuç, yol)` | Var mı → 1/0 |
| `file_read(d, yol)` | Dosyayı oku |
| `ast_do(d, "as komut")` | AST komutunu çalıştır, çıktıyı d'ye al. `edit login logout reboot shutdown asscript aspi clear` reddedilir |

## 5. Sınırlar

| Sınır | Değer |
|---|---|
| Satır sayısı | 4096 |
| Satır uzunluğu | 256 |
| Kaynak boyutu | 49152 B |
| Dosya boyutu (FS) | 4096 B |
| Değişken | 96 |
| Fonksiyon | 32 |
| İsim uzunluğu | 32 |
| Metin değeri | 255 |
| Döngü adımı | 1.000.000 |
| Çağrı derinliği | 16 |
| ASPI paketi | 96 dosya, 2 MiB arşiv |

## 6. Paket (ASPI) kuralları

- Her `.as` dosyası kurulumda ayrı ayrı sözdizimi kontrolünden geçer. Kontrol modu if/else'in iki dalını ve fonksiyon gövdelerini çalıştırmadan değerlendirir; bir fonksiyon, aynı dosyada tanımlanmamış değişkeni kullanırsa "Variable not found." verir.
- Paket komutu: `<komut>.as`, `main.as` veya `<Paket>.as` çalışır; diğer dosyaların içeriği bağlam olarak eklenir.

## 7. Hata iletileri

Unknown syntax. · Variable not found. · Unknown variable type. · Variable name missing. · Invalid variable name. · Too many variables. · Invalid printc syntax. · Unknown variable or invalid expression in printc(). · Invalid inputc syntax. · inputc() variable not found. · Invalid condition. / Invalid condition values. / Invalid string condition. · Invalid fr syntax. / Invalid fr initialization. / Invalid fr increment (use: i = i + 1). · Unclosed function. · Invalid function name. · Too many functions. · Function not found. · Maximum call depth exceeded. · Loop limit exceeded. · else without matching if. · Line is too long. · Too many lines. · Script too large. · Empty AstraSript file. · Use: use PackageName; · Package is not installed (as aspi install). · gui_/str_/file_ hataları (ör. "File would exceed 4096 bytes.", "Not signed in (as login).")

## 8. Bilinen eksikler (güncelleme için öneri listesi)

1. Fonksiyonlara parametre ve dönüş değeri (FroggyCode'da 30/32 fonksiyon sınırına yaklaştık).
2. Metin birleştirme operatörü (`s = a + b`) ve metin ifadeleri (`printc` içinde de).
3. `} else {` ve `} else if` aynı satırda.
4. Tek `vr` ile çoklu bildirim (`vr inter a, b;`).
5. `i++`, `+=` gibi kısaltmalar; `break` / `continue`.
6. Diziler / listeler.
7. Fonksiyon sayısı (32) ve değişken sayısı (96) sınırları, 4096 B dosya sınırı.
8. Türkçe karakterler (klavye yolu yalnız ASCII).
9. Script içinden başka script/`aspi` çalıştıramama.
10. Rastgele sayı, zaman, ses gibi yerleşikler yok.
11. Tek dosyada kontrol modu değişkenleri: fonksiyonlar dosyalar arası değişken görmez (check hatası).

Bunlar yalnızca öneridir; hiçbiri uygulanmış değildir.
