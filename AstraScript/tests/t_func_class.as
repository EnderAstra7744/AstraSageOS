start;
func greet(name, greeting="Hi") { return greeting + " " + name; end; };
func total(*args) { t = 0; for a in args { t += a; end; }; return t; end; };
func info(**kw) { return len(kw); end; };
func make_counter() {
    c = 0;
    func inc() { nonlocal c; c += 1; return c; end; };
    return inc;
    end;
};
class Insan {
    func init(self, ad, yas) { self.ad = ad; self.yas = yas; end; };
    func selam(self) { return "Ben " + self.ad; end; };
};
class Ogrenci(Insan) {
    func init(self, ad, yas, okul) { super().init(ad, yas); self.okul = okul; end; };
    func selam(self) { return super().selam() + " / " + self.okul; end; };
};
func main(void) {
    assert greet("Ali") == "Hi Ali" and greet("Ali", greeting="Yo") == "Yo Ali";
    assert total(1, 2, 3) == 6 and info(a=1, b=2) == 2;
    c = make_counter(); c(); assert c() == 2;
    sq = lambda x: x * x;
    assert sq(5) == 25;
    i = Insan("Ali", 30);
    assert i.ad == "Ali" and i.selam() == "Ben Ali";
    o = Ogrenci("Veli", 20, "ODTU");
    assert o.selam() == "Ben Veli / ODTU" and isinstance(o, Insan);
    try { x = 1 / 0; } except ZeroDivisionError as e { assert True; };
    try { raise ValueError("bad"); } except ValueError as e { assert e.msg == "bad"; };
    try { assert False; } except AssertionError { pass; };
    printc("func/class ok");
    end;
};
end;
