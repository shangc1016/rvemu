在本项目中使用到了jit方法来加速模拟器的执行效率。

使用jit方法的前提是利用到了程序执行的局部性原理。因为程序本身包含了很多相似的局部部分，或者说程序中存在大量的循环、跳转逻辑，使得具有这种时间、空间上的局部性。即当前执行的代码，可能会在不就得将来重复执行，当前访问的数据可能会在不久的将来重复访问。  
因此出于程序执行时间局部性的考虑，有理由相信机器可能在不久的将来会重复执行当前pc所指向的这段代码。所以在本项目中使用了哈希表的方式把需要模拟执行的`<pc, 在本地机器上执行的指令>`缓存起来。在模拟target架构的程序执行的时候，首先去检查缓存中是否有相同的pc地址，如果有的话，我们就可以直接执行host架构的相同逻辑的模拟程序，不用再一条条的模拟取指、译码、执行这个循环了。从而达到提高性能的作用。  


首先分析一下jit执行的流程
```c
// 模拟器执行的函数
enum exit_reason_t machine_step(machine_t *m){
    while(true) {
        bool hot = true;

        // 根据当前机器的pc指针，在jit cache中检索，看看能不能找到相应的host的可执行代码片段
        u8 *code = cache_lookup(m->cache, m->state.pc);
        if (code == NULL) {
            // 找不到的话，更新这段代码的hot计数值，也就是对哈希表中pc这一项的hot值自增
            hot = cache_hot(m->cache, m->state.pc);
            if (hot) {
                // 如果pc这段代码是hot的，而且上面的cache_lookup中没找到相应的jit缓存
                // 那就说明pc这块代码还没编译生成host的代码片段，那就调用machine_genblock
                // 编译生成这段host代码片段。
                str_t source = machine_genblock(m);
                // source就是host的代码的字符串
                // 然后编译成一段代码code，而且把jit的代码已经加入到jit cache中
                // 在`machine_compile`函数中，使用了匿名管道的方式，把source编译生成的
                // elf链接文件拷贝到machine.cache中，并且在哈希表的pc项的offset记录
                // 这个elf文件在整个jit cache中的偏移

                // 最后的code就是jit cachezhong elf代码片段的内存地址
                code = machine_compile(m, source);
            }
        }

        // 如果不是hot，就还是按照取指、译码、执行这样一步一步来做
        if (!hot) {
            code = (u8 *)exec_block_interp;
        }
        while (true) {
            m->state.exit_reason = none;
            // 在这个地方，把code类型转换为函数指针，直接调用host缓存的函数
            // 不需要模拟执行了
            ((exec_block_func_t)code)(&m->state);
            assert(m->state.exit_reason != none);

            // 
            // 这儿有一个问题，给一个pc指针，如果要用jit的方式将host的指令编译的话，
            // 怎么选择jit的范围。最开始想到的是jit能转换的肯定是一段没有跳转的代码
            // 所以就是遇到跳转指令、ecall系统调用指令的时候停下来。
            // 从pc指针到停下来的范围内都是jit可以编译的一段代码。

            if (m->state.exit_reason == indirect_branch ||
                m->state.exit_reason == direct_branch ) {
                code = cache_lookup(m->cache, m->state.reenter_pc);
                if (code != NULL) continue;
            }

            if (m->state.exit_reason == interp) {
                m->state.pc = m->state.reenter_pc;
                code = (u8 *)exec_block_interp;
                continue;
            }

            break;
        }

        m->state.pc = m->state.reenter_pc;
        switch (m->state.exit_reason) {
        case direct_branch:
        case indirect_branch:
            // continue execution
            break;
        case ecall:
            return ecall;
        default:
            unreachable();
        }
    }
}
```

还有一些问题
- jit生成host的代码的时候，根据pc指针怎么确定要转换的代码范围的？是不是遇到跳转、ecall、还有读写csr等架构相关的寄存器的时候停止吗？这块代码对应`codegen.c/machine_genblock`函数。


- `machine_genblock`生成的jit热点代码缓存的范围是怎么确定的？