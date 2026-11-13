# Phase 152 plan: the AI assistant, solved rather than benched

The owner, 2026-11-13: "I am not convinced that we solved it, I think we just benched it." **The lowest priority on the roadmap, by the owner's word.** Status: `PLANNED`.

## The honest state
- **Two of P3.6's three options are done.**
  - C (2026-10-17): the app's own numbers go into the prompt, gated by `tests/ai_context_facts.cpp`.
  - B: the assistant reads the project's notes and session logs.
  - Both made it more honest. Neither made it more capable.
- **The model has not changed since it was boxed in.** It is llama3.2:3b on StarMachine's CPU, and the name is hard-coded (`AIAssistant.h:108`). It is held to app questions because outside that box it was confidently wrong (planetary exposure "1-10 seconds", out by three orders of magnitude).
- **The bigger model (option A) never started.**
  - The GPU route on StarMachine crashes: D4, driver 546.01, "device kernel image is invalid".
  - The written check that was meant to decide when the box could widen was never written.
- **So the owner is right.** It was made safe and set aside, not solved.

## Two scenarios, one assistant
The owner's direction:
- **Preferred:** the best model the machine it runs on can handle.
- **Fallback:** the model StarMachine can run with its current setup.

Both must work, and both are tested.

| | Preferred | StarMachine fallback |
|---|---|---|
| Benchmark machine | The owner's other PC, RTX 4090 (24 GB) | StarMachine: CPU, or the RTX 2060 (6 GB) if D4 is ever solved |
| Model class | The largest that passes the check and fits 24 GB at a usable speed (a ~30B-class model at 4-bit is about 20 GB; inferred, measured in 152.3) | 3B to 8B at 4-bit, chosen by the same check |
| Reached how | Ollama on that machine. The app can point at it over the home network when it is on; that is still not the internet, so rule 7 (airplane mode) holds | Ollama on StarMachine itself, always available, including in the field |

Specific model names are not fixed here. Local models change month to month, so the candidates are chosen when 152.3 runs and judged by the check, not by reputation.

## Stages
- **152.0 The written check** (`tools/ai_eval` and a question file in the repo).
  - Questions with known answers, scored automatically (numbers within tolerance, required phrases, forbidden ones):
    - the rig's numbers (plate scale, field of view, the exposure slider's range);
    - what controls do (ROI, FULL, GATE, the stretch);
    - planetary exposure in milliseconds, the question that caught the 3B out;
    - tonight's Jupiter events from the snapshot (phase 144);
    - "what happened last session" from the logs;
    - questions it must decline or send to the safety text.
  - It reports per category, so the scope can widen one category at a time, as P3.6 required.
  - It runs against any Ollama host and model from the command line. It is not in CI, because CI has no model; it is run by the owner on each machine.
- **152.1 The model as a setting, and the fallback.**
  - rig.ini gains:
    - `AiPreferredHost` and `AiPreferredModel`;
    - `AiFallbackHost` and `AiFallbackModel` (default this PC and llama3.2:3b, today's behaviour);
    - `AiScope` (the categories the check passed).
  - At start and every few minutes, the app asks the preferred host whether the model is there (Ollama's `/api/tags`; to be confirmed against the Ollama API when built). It uses the preferred model if it answers, else the fallback, and the chat panel says which one is answering.
  - A question already under way is never switched mid-answer.
- **152.2 StarMachine's environment, fixed where it hurts.** Owner actions with exact steps, from D4's 2026-11-01 measurement:
  - `OLLAMA_CONTEXT_LENGTH=262144` makes a 2 GB model take 17 GB of RAM. Set it to what the prompt needs (the app's prompt is bounded near 8 KB of context plus history; size it with 152.0's longest case).
  - `OLLAMA_HOST=0.0.0.0` listens beyond this PC. Bind it to the PC, unless the owner wants the 4090 PC to reach it.
  - `OLLAMA_LLM_LIBRARY=cpu` stays until the GPU crash is solved. Retrying the RTX 2060 after a driver update is listed, and is not a precondition.
- **152.3 Benchmark both scenarios.** On the 4090 PC and on StarMachine, for each candidate model, record:
  - the check's score per category;
  - time to first token with the full prompt (the prompt cost matters more than generation here);
  - tokens a second;
  - memory used.
  - Then pick the preferred and fallback models, and set `AiScope` to the categories each passes.
- **152.4 Widen the scope, category by category.** Only for categories the model in use passed. The safety section stays forced and never depends on the model, as P3.6 required.
- **152.5 The advisory loop** (the one-sentence advice every few minutes) uses whichever model is answering, and is tested in both scenarios: does it phrase the app's own concerns correctly, and does it stay silent when there are none?

## Testing, both ways (the owner's requirement)
- Every stage that touches behaviour is checked twice, with the preferred model on the 4090 PC and with the fallback on StarMachine.
- Plus the switch between them: the preferred host goes away mid-session (the PC is turned off) and the fallback takes the next question; it comes back and is used again.
- The prompt-building code stays gated in CI by `ai_context_facts`, as now.

## Versioning
Every PR bumps PATCH; the PR that completes the phase bumps MINOR ([`ROADMAP-Next.md`](ROADMAP-Next.md#next-in-order)).
