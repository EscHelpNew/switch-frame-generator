"""Summarize real PC GPU measurements. Does not connect to or estimate a Switch."""
from collections import Counter
from pathlib import Path
import csv
import json
import math
import statistics

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build"
WARMUP = 2.0


def percentile(values, fraction):
    values = sorted(values)
    if not values:
        return 0.0
    position = (len(values) - 1) * fraction
    low = math.floor(position)
    high = math.ceil(position)
    return values[low] + (values[high] - values[low]) * (position - low)


def distribution(values):
    return {"mean": statistics.mean(values) if values else 0,
            "median": percentile(values, 0.5), "p95": percentile(values, 0.95),
            "max": max(values, default=0), "min": min(values, default=0)}


def analyze(label):
    meta = json.loads((OUT / f"perf-{label}-meta.json").read_text())
    with (OUT / f"perf-{label}-source.csv").open(encoding="utf-8") as file:
        source = list(csv.DictReader(file))
    with (OUT / f"perf-{label}-present.csv").open(encoding="utf-8") as file:
        shown = list(csv.DictReader(file))
    end = min(float(source[-1]["wall_s"]), float(shown[-1]["wall_s"]))
    source = [r for r in source if WARMUP <= float(r["wall_s"]) <= end]
    shown = [r for r in shown if WARMUP <= float(r["wall_s"]) <= end]
    if len(source) < 10 or len(shown) < 10:
        raise ValueError(f"Not enough warmed-up data for {label}")
    seconds = end - WARMUP
    kinds = Counter(r["kind"] for r in shown)
    generated_by_source = Counter(r["source_id"] for r in shown if r["kind"] == "generated")
    if max(generated_by_source.values(), default=0) > 1:
        raise ValueError("More than one generated image displayed for one source")
    if any(int(r["generated_computed"]) > 1 for r in source):
        raise ValueError("More than one generated image computed for one source")
    fg = [float(r["lsfg_gpu_ms"]) for r in source]
    cpu_wall = float(source[-1]["wall_s"]) - float(source[0]["wall_s"])
    cpu_ms = float(source[-1]["process_cpu_ms"]) - float(source[0]["process_cpu_ms"])
    logical = [float(r["logical_s"]) for r in source]
    source_fps = [1 / (b - a) for a, b in zip(logical, logical[1:]) if b > a]
    result = {
        "label": label, "metadata": meta, "warmup_s": WARMUP, "measured_s": seconds,
        "source_fps": len(source) / seconds, "output_calls_fps": len(shown) / seconds,
        "unique_output_fps": (kinds["real"] + kinds["generated"]) / seconds,
        "generated_shown_fps": kinds["generated"] / seconds, "repeat_fps": kinds["repeat"] / seconds,
        "generated_computed_fps": sum(int(r["generated_computed"]) for r in source) / seconds,
        "max_generated_per_source": max(generated_by_source.values(), default=0),
        "logical_source_fps": distribution(source_fps), "gpu_lsfg_ms_per_source": distribution(fg),
        "gpu_scene_ms_per_source": distribution([float(r["scene_gpu_ms"]) for r in source]),
        "gpu_lsfg_ms_per_second": sum(fg) / seconds,
        "gpu_lsfg_time_fraction_percent": sum(fg) / (seconds * 10),
        "cpu_equivalent_one_core_percent": cpu_ms / (cpu_wall * 10),
        "cpu_total_machine_percent": cpu_ms / (cpu_wall * 10 * meta["logical_processors"]),
        "rss_bytes": distribution([int(r["rss_bytes"]) for r in source]),
        "swap_return_interval_ms": distribution([float(r["interval_ms"]) for r in shown]),
        "swap_submit_interval_ms": distribution([float(r["submit_interval_ms"]) for r in shown if float(r["submit_interval_ms"]) > 0]),
        "swap_block_ms": distribution([float(r["swap_block_ms"]) for r in shown]),
        "return_deadline_delta_ms": distribution([float(r["lateness_ms"]) for r in shown]),
        "queue_depth_max": max(int(r["queue_depth"]) for r in shown),
        "rows": {"source": len(source), "shown": len(shown), "kinds": dict(kinds)},
    }
    return result


def main():
    data = [analyze(label) for label in ("fixed2x", "variable-off", "adaptive40")]
    (OUT / "performance-summary.json").write_text(json.dumps(data, indent=2) + "\n")
    lines = [
        "# Стоимость LSFG и адаптивный вывод 40 FPS",
        "", "Измерено 25 октября 2026 на ПК, в сцене с мячом при **1280×720**. "
        "Первый аппаратный запуск Switch описан отдельно в [SWITCH_FIRST_RUN_RU.md](SWITCH_FIRST_RUN_RU.md).", "",
        f"GPU: **{data[0]['metadata']['gpu']}**; {data[0]['metadata']['gl']}. "
        f"Монитор: {data[0]['metadata']['display_hz']} Гц; CPU: {data[0]['metadata']['logical_processors']} логических процессоров.",
        "", "Каждый режим измерен отдельным процессом; первые 2 секунды после подготовки "
        "шейдеров исключены. Частоты GPU/CPU не фиксировались. GPU timestamps охватывают "
        "проходы LSFG, отдельно от копирования в очередь, HUD и presentation; CPU submission wall time включает ожидание glFinish и не называется CPU busy. "
        "Кадры без повторов посчитаны как real + generated по метаданным очереди, без проверки уникальности пикселей или захвата дисплея.",
        "", "## Частота и стоимость", "",
        "| Режим | Исходные FPS | Вывод, вызовов/с | Уникальные/с | FG показано/с | Повторы/с | GPU LSFG среднее / p95, мс на исходный кадр |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    names = {"fixed2x": "Фиксированные 30 → 60", "variable-off": "20–30, FG выключен, таймер 40", "adaptive40": "20–30 → 40, максимум 1 FG"}
    for r in data:
        gpu = r["gpu_lsfg_ms_per_source"]
        lines.append(f"| {names[r['label']]} | {r['source_fps']:.2f} | {r['output_calls_fps']:.2f} | "
                     f"{r['unique_output_fps']:.2f} | {r['generated_shown_fps']:.2f} | {r['repeat_fps']:.2f} | {gpu['mean']:.3f} / {gpu['p95']:.3f} |")
    lines += ["", "| Режим | GPU LSFG, мс/сек | Доля измеренного GPU-времени | CPU, эквивалент одного ядра | CPU от всей машины | RSS процесса, MiB |",
              "|---|---:|---:|---:|---:|---:|"]
    for r in data:
        lines.append(f"| {names[r['label']]} | {r['gpu_lsfg_ms_per_second']:.1f} | {r['gpu_lsfg_time_fraction_percent']:.2f}% | "
                     f"{r['cpu_equivalent_one_core_percent']:.2f}% | {r['cpu_total_machine_percent']:.2f}% | {r['rss_bytes']['median']/2**20:.1f} |")
    lines += ["", "Доля GPU-времени — сумма измеренных интервалов LSFG за секунду, делённая на секунду. "
              "Это не показание общей загрузки GPU из диспетчера задач и не расход электроэнергии. "
              "CPU включает все потоки процесса, в том числе работу драйвера. RSS — оперативная память процесса, не видеопамять.",
              "", "## Память и цена буферизации", ""]
    a = data[-1]
    graph_bytes = a["metadata"]["texture_bytes"]
    queue_bytes = a["metadata"]["queue_texture_bytes"]
    lines += [f"- Изображения LSFG: **{graph_bytes / 2**20:.2f} MiB**, 167 текстур.",
              f"- Резерв очереди: **{queue_bytes / 2**20:.2f} MiB**, максимум 12 копий 720p.",
              f"- Вместе: **{(graph_bytes + queue_bytes) / 2**20:.2f} MiB логического хранения текстур**. "
              "Это не полный расход VRAM: back buffers окна, shader binaries, выравнивание и память драйвера сюда не входят.",
              f"- На измеренном участке максимум ожидающих кадров: {a['queue_depth_max']}.",
              "- Отключение FG сохраняет выделенную память для быстрого переключения; GPU-проходы LSFG при этом не исполняются.",
              "- Буфер опережающей подготовки: 75 мс. Показ исходного кадра округляется вверх к сетке 25 мс: "
              "дополнительное смещение меньше 25 мс, плюс задержка дисплея/композитора.",
              "", "## Равномерность вывода", "",
              "| Режим | Интервал вызовов SwapBuffers: медиана / p95, мс | Интервал возврата SwapBuffers: медиана / p95, мс |",
              "|---|---:|---:|"]
    for r in data:
        s, e = r["swap_submit_interval_ms"], r["swap_return_interval_ms"]
        lines.append(f"| {names[r['label']]} | {s['median']:.2f} / {s['p95']:.2f} | {e['median']:.2f} / {e['p95']:.2f} |")
    lines += ["", "Цель адаптивного планировщика — один слот каждые 25 мс. Вывод имеет приоритет "
              "над подготовкой исходного кадра. Использован high-resolution waitable timer Windows; "
              "кадр передаётся за 8 мс до целевого слота, оставляя запас драйверу. На этом 120-Гц мониторе запрошен WGL swap interval 3. "
              "Windows/DWM и драйвер могут менять фактический момент показа: вызовы и возвраты SwapBuffers не являются измерением scanout. "
              "Разброс выше указан явно; среднее 40 FPS само по себе не доказывает идеальную плавность.",
              "", "## Как соблюдается ограничение", "",
              "При 20 исходных FPS нужен один промежуточный кадр на каждый реальный. "
              "При 30 FPS дополнительный кадр нужен только части пар. Тесты проверяют 20, 25, 30 FPS, "
              "нерегулярные интервалы и задержки; в измерениях максимум показанных FG для одного source_id равен 1.",
              f"В адаптивном замере вычислялось {a['generated_computed_fps']:.2f} FG/с, показывалось {a['generated_shown_fps']:.2f} FG/с. "
              "Текущий алгоритм исполняет проходы на каждом исходном кадре для непрерывности внутренней истории, "
              "даже когда результат не нужен для вывода. Поэтому стоимость не равна цене только показанных промежуточных кадров.",
              "Ниже 20 исходных FPS либо при превышении GPU-бюджета лимит 1 FG не позволяет гарантировать 40 уникальных кадров: "
              "появляются отдельно учитываемые повторы. Просроченные исходные отсчёты пропускаются, а не накапливаются в бесконечную очередь.",
              "", "## Ввод", "",
              "В адаптивном режиме ввод проверяется между выводами. Клик по видимому мячу "
              "сбрасывает устаревшую очередь и вызывает новый реальный кадр. Во время восстановления истории "
              "могут появляться повторы: они не засчитываются как сгенерированные кадры. Это реакция движка на ввод, "
              "а не изменение смысла нажатия внутри нейросети LSFG.",
              "", "## Повторение замера", "",
              "```powershell", "py -3 build.py --host-only", ".\\tools\\benchmark.ps1 -Seconds 14", "py -3 tools/report_performance.py", "```",
              "", "Перед измерением закрыть интерактивное превью. Сырые данные: `build/perf-*-source.csv`, "
              "`build/perf-*-present.csv`, `build/perf-*-meta.json`. Сводка: `build/performance-summary.json`. "
              "Другие приложения и режим питания могут изменить результаты."]
    path = ROOT / "docs/PERFORMANCE_RU.md"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(path)
    for r in data:
        print(r["label"], "source", round(r["source_fps"], 2), "output", round(r["output_calls_fps"], 2),
              "unique", round(r["unique_output_fps"], 2), "GPU mean ms", round(r["gpu_lsfg_ms_per_source"]["mean"], 3))


if __name__ == "__main__":
    main()
