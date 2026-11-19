start;
class Ctx { func init(self) { self.n = 0; end; }; func enter(self) { printc("in"); return self; end; }; func exit(self) { printc("out"); end; }; };
func main(void) {
    with Ctx() as c { printc("body"); end; };
    ask { x = [1][5]; end; } error_for IndexError as e { printc("caught", e.msg); end; };
    ask { push_error ValueError("boom"); end; } error_for ValueError as e { printc("caught2", e.msg); end; };
    try { y = {}["k"]; } except KeyError as e { printc("key", e.msg); };
    printc(1/0);
    end;
};
end;
