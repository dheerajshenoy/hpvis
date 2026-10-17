// Kept apart from the Qt code: CLASS's headers define many short macros.
#include "classwrap.hpp"

#include "class.h"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <numbers>
#include <stdexcept>
#include <string>

namespace
{
// Everything CLASS allocated, released on any exit
struct Class
{
    precision pr{};
    background ba{};
    thermodynamics th{};
    perturbations pt{};
    primordial pm{};
    fourier fo{};
    transfer tr{};
    harmonic hr{};
    lensing le{};
    distortions sd{};
    output op{};
    file_content fc{};
    bool hasFc = false;
    int stage  = 0; // modules initialised, in order

    ~Class()
    {
        if (stage >= 10) distortions_free(&sd);
        if (stage >= 9) lensing_free(&le);
        if (stage >= 8) harmonic_free(&hr);
        if (stage >= 7) transfer_free(&tr);
        if (stage >= 6) fourier_free(&fo);
        if (stage >= 5) primordial_free(&pm);
        if (stage >= 4) perturbations_free(&pt);
        if (stage >= 3) thermodynamics_free(&th);
        if (stage >= 2) background_free(&ba);
        if (hasFc) parser_free(&fc);
    }
};

[[noreturn]] void
fail(const char *where, const char *message)
{
    throw std::runtime_error(std::string("CLASS (") + where + "): " + message);
}
} // namespace

ClassSpectra
computeClassSpectra(const CosmoParams &p, int lmax)
{
    Class c;
    ErrorMsg err;

    // The input as CLASS reads it from a parameter file
    struct Entry { std::string name, value; };
    std::vector<Entry> entries;
    char buf[64];
    auto num = [&](double v)
    {
        std::snprintf(buf, sizeof buf, "%.10g", v);
        return std::string(buf);
    };
    auto add = [&](const char *name, std::string value)
    {
        entries.push_back({name, std::move(value)});
    };
    add("H0", num(p.H0));
    add("omega_b", num(p.ombh2));
    add("omega_cdm", num(p.omch2));
    add("tau_reio", num(p.tau));
    add("ln_A_s_1e10", num(p.lnAs));
    add("n_s", num(p.ns));
    if (p.alphaS != 0.0)
        add("alpha_s", num(p.alphaS));

    // Neutrinos: one massive species (it counts 1.0132 in N_eff, as in the
    // Planck 2018 baseline) and the rest massless
    if (p.mnu > 0.0)
    {
        add("N_ur", num(std::max(0.0, p.neff - 1.0132)));
        add("N_ncdm", "1");
        add("m_ncdm", num(p.mnu));
        add("T_ncdm", "0.71611");
    }
    else
        add("N_ur", num(p.neff));

    if (p.omk != 0.0)
        add("Omega_k", num(p.omk));
    if (p.w0 != -1.0 || p.wa != 0.0)
    {
        // The dark energy fluid takes the place of the cosmological constant
        add("Omega_Lambda", "0");
        add("w0_fld", num(p.w0));
        add("wa_fld", num(p.wa));
    }
    add("T_cmb", num(p.tcmb));
    if (!p.yheFromBbn)
        add("YHe", num(p.yhe));

    if (p.r > 0.0)
    {
        add("modes", "s,t");
        add("r", num(p.r));
        add("n_t", p.ntConsistency ? "scc" : num(p.nt));
        add("l_max_tensors", "500");
    }
    add("output", "tCl,pCl,lCl");
    add("lensing", "yes");
    add("l_max_scalars", std::to_string(lmax));
    const int n = int(entries.size());
    char name[] = "hpvis";
    if (parser_init(&c.fc, n, name, err) == _FAILURE_)
        fail("parser", err);
    c.hasFc = true;
    for (int i = 0; i < n; ++i)
    {
        std::strncpy(c.fc.name[i], entries[i].name.c_str(), sizeof(FileArg) - 1);
        std::strncpy(c.fc.value[i], entries[i].value.c_str(),
                     sizeof(FileArg) - 1);
        c.fc.read[i] = _FALSE_;
    }

    if (input_read_from_file(&c.fc, &c.pr, &c.ba, &c.th, &c.pt, &c.tr, &c.pm,
                             &c.hr, &c.fo, &c.le, &c.sd, &c.op, err)
        == _FAILURE_)
        fail("input", err);
    c.stage = 1;
    if (background_init(&c.pr, &c.ba) == _FAILURE_)
        fail("background", c.ba.error_message);
    c.stage = 2;
    if (thermodynamics_init(&c.pr, &c.ba, &c.th) == _FAILURE_)
        fail("thermodynamics", c.th.error_message);
    c.stage = 3;
    if (perturbations_init(&c.pr, &c.ba, &c.th, &c.pt) == _FAILURE_)
        fail("perturbations", c.pt.error_message);
    c.stage = 4;
    if (primordial_init(&c.pr, &c.pt, &c.pm) == _FAILURE_)
        fail("primordial", c.pm.error_message);
    c.stage = 5;
    if (fourier_init(&c.pr, &c.ba, &c.th, &c.pt, &c.pm, &c.fo) == _FAILURE_)
        fail("fourier", c.fo.error_message);
    c.stage = 6;
    if (transfer_init(&c.pr, &c.ba, &c.th, &c.pt, &c.fo, &c.tr) == _FAILURE_)
        fail("transfer", c.tr.error_message);
    c.stage = 7;
    if (harmonic_init(&c.pr, &c.ba, &c.pt, &c.pm, &c.fo, &c.tr, &c.hr)
        == _FAILURE_)
        fail("harmonic", c.hr.error_message);
    c.stage = 8;
    if (lensing_init(&c.pr, &c.pt, &c.hr, &c.fo, &c.le) == _FAILURE_)
        fail("lensing", c.le.error_message);
    c.stage = 9;
    if (!c.le.has_tt || !c.le.has_ee || !c.le.has_te || !c.le.has_bb)
        fail("lensing", "the lensed spectra are missing");

    const int top = std::min(lmax, int(c.le.l_lensed_max));
    // C_l is dimensionless (in units of T_cmb^2): to uK^2 and to D_l
    const double tcmb = c.ba.T_cmb * 1e6;
    ClassSpectra s;
    s.lmax = top;
    for (auto *v : {&s.TT, &s.TE, &s.EE, &s.BB})
        v->assign(size_t(top) + 1, 0.0);
    std::vector<double> cl(size_t(c.le.lt_size));
    for (int l = 2; l <= top; ++l)
    {
        if (lensing_cl_at_l(&c.le, l, cl.data()) == _FAILURE_)
            fail("lensing", c.le.error_message);
        const double f = double(l) * (l + 1) / (2.0 * std::numbers::pi)
                         * tcmb * tcmb;
        s.TT[l] = cl[c.le.index_lt_tt] * f;
        s.TE[l] = cl[c.le.index_lt_te] * f;
        s.EE[l] = cl[c.le.index_lt_ee] * f;
        s.BB[l] = cl[c.le.index_lt_bb] * f;
    }
    return s;
}
