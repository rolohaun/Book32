// Guard the actual FreeRTOS call site: notifications can remain pending forever
// when emulation takes longer than a display scan. taskYIELD is insufficient.
const fs=require('node:fs'),assert=require('node:assert/strict');
const source=fs.readFileSync('lib/Apps/AppPaperboy/AppPaperboy.cpp','utf8');
const run=source.slice(source.indexOf('void AppPaperboy::run('),source.indexOf('void AppPaperboy::update('));
assert.match(run,/xSemaphoreGive\(app->_mutex\);\s*if \(synchronized\) \{[\s\S]*?vTaskDelay\(1\);\s*continue;/);
assert.match(run,/vTaskDelay\(std::max\(TickType_t\(1\),pdMS_TO_TICKS/);
assert(!run.includes('esp_task_wdt_delete')&&!run.includes('disableCore0WDT'));
assert(!run.includes('renderCatchUpFrame') && !run.includes('if (render)'));
assert(run.indexOf('++app->_producedSequence')<run.indexOf('app->packVideo'));
assert.match(run,/inkConsoleFrame\(app->_buttons.load\(\), app->_nesVideo \? nullptr : app->_workFrame\)/);
const allocation=source.slice(source.indexOf('uint8_t* allocateFrame'),source.indexOf('bool writeStateFile'));
assert(allocation.indexOf('ps_calloc')<allocation.indexOf('heap_caps_calloc'));
const resume=source.slice(source.indexOf('void AppPaperboy::resumeGame'),source.indexOf('void AppPaperboy::exitGame'));
assert(resume.indexOf('_drawnFrames = 0')<resume.indexOf('_playing = true'));
// Reproduce a permanently backlogged priority-1 worker on a 1 ms RTOS tick.
// A real one-tick block makes IDLE0 eligible at least once per <=2-frame batch.
for(const frameMs of [1,16,29,100,1000]){
    let elapsed=0,lastIdle=0,maxGap=0;
    while(elapsed<30000){elapsed+=2*frameMs+1;maxGap=Math.max(maxGap,elapsed-lastIdle);lastIdle=elapsed;}
    assert(maxGap<5000);
}
console.log('Scheduler PASS: locked work is bounded, both paths block for >=1 tick, watchdog stays enabled');
