# 07 — Go-to-Market & Monetization

This is the business context for the build. It is not code, but it dictates what to build first and
why. Keep the engineering honest about the goal: adoption in team CI, then paid team/enterprise
features.

## 1. Positioning

**One line:** "Semantic version control and CI for circuit design — catch the commit that changed the
circuit, not just the drawing."

Against the field:
- vs **visual-diff tools** (KiRi, kdiff, plotkicadsch, typecad-gitdiff): they diff pictures; NetDiff
  diffs connectivity. Semantic, low-noise, gates CI.
- vs **CADLAB.io** (commercial Git + visual diff, cloud): NetDiff adds semantic diff and runs
  offline / self-hosted for IP-sensitive teams.
- vs **AI schematic reviewers** (Traceformer, NextPCB, galvano): different job — objective
  "what changed" vs subjective "is this good." NetDiff is deterministic and can feed a review layer,
  not compete on EE judgment.

## 2. Wedge → expansion (the company thesis)

- **Wedge (Phase 1–2):** objective connectivity diff + CI gating. Pure software strength; no EE
  judgment required; leverages the existing engine. Lands with individual engineers and small teams.
- **Expansion (Phase 3):** the team platform — hosted PR bot, web review, dashboards, self-hosted
  tier. This is where recurring revenue and defensibility come from.
- **Honest caveat:** diff alone may be a feature, not a company. It becomes a company only as the
  wedge into team CI/review. Validate demand for the platform with design partners before over-investing in Phase 3.

## 3. Target segments (in priority order)

1. **Small/mid hardware teams on KiCad + Git (2–50 eng).** Feel the pain, adopt bottom-up, can pay
   per-seat.
2. **IP-sensitive orgs (defense, medical, aerospace, industrial).** Cannot use cloud reviewers;
   value the offline / self-hosted tier highly; larger contracts, longer sales cycles. This is the
   differentiated, defensible segment — but you need credibility (a hardware co-founder/advisor helps).
3. **Open-source hardware projects.** Free tier; drives adoption, credibility, and inbound.

Deprioritize: solo hobbyists (won't pay), non-KiCad enterprise EDA (v1 is KiCad-only).

## 4. Pricing model (initial hypothesis — validate, don't assume)

- **Free / OSS:** the CLI, KiCad plugin, GitHub Action, and public-repo CI. This drives adoption and
  is the top of funnel. Open-source the core engine (subject to the licensing resolution in PRD §9).
- **Team (paid, per-seat or per-repo, monthly):** hosted PR bot at org scale, web review UI, team
  dashboards, private-repo CI convenience, history/analytics.
- **Enterprise / self-hosted (annual contract):** the on-prem Docker image, SSO, support/SLA. Priced
  for the IP-sensitive segment. This is where the real revenue is.
- Optional AI "explain this change" add-on as a metered feature.

Do not hard-code prices in the product; put them behind config/Stripe so you can iterate.

## 5. Distribution

- **KiCad Plugin & Content Manager** — free reach to the entire KiCad userbase.
- **GitHub Marketplace** — list the Action.
- **The client-side WASM demo** on the existing Vercel site — "drop two schematics, see the diff," no
  signup, no upload. This is the single best top-of-funnel asset; make it excellent.
- **Community:** the KiCad forum and r/PrintedCircuitBoard already have threads asking for exactly
  this. Post the OSS CLI there; that is also where you recruit design partners.

## 6. The non-negotiable go-to-market prerequisite

Get **3–5 design-partner hardware teams** using the free CLI on real PRs before building Phase 3.
They validate usefulness, surface noise, and are your first references and (ideally) your path to a
hardware co-founder. If you cannot get one team to try a free tool that solves a real pain, that is
the signal to reassess before writing the paid platform.

## 7. Metrics that matter (business)

- Repos running NetDiff in CI weekly (north star).
- Design partners actively using it on real PRs.
- Free→paid team conversion.
- Logos in the IP-sensitive segment (the defensible revenue).

## 8. Sequencing rule

Build order = value order: correctness (Phase 0) → sellable MVP CLI/CI (Phase 1) → distribution
(Phase 2) → monetized platform (Phase 3). Do not build billing before you have users; do not build
the AI layer before the deterministic core is trusted.
