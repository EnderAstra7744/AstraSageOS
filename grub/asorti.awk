function asorti(src, dest,    n, k, i, j, t) {
    n = 0
    for (k in src) { n++; dest[n] = k }
    for (i = 2; i <= n; i++) { t = dest[i]; j = i - 1; while (j >= 1 && dest[j] > t) { dest[j+1] = dest[j]; j-- } dest[j+1] = t }
    return n
}
