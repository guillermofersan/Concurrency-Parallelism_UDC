-module(break_md5).
-define(PASS_LEN, 6).
-define(UPDATE_BAR_GAP, 100000).
-define(BAR_SIZE, 40).
-define(NUMBER_PROCS,3).

-export([break_md5s/1, 
         break_md5/1,
         pass_to_num/1,
         num_to_pass/1,
         num_to_hex_string/1,
         hex_string_to_num/1
        ]).

-export([break_md5/5]).
-export([start_procs/7]).
-export([progress_loop/3]).

% Base ^ Exp

pow_aux(_Base, Pow, 0) ->
    Pow;
pow_aux(Base, Pow, Exp) when Exp rem 2 == 0 ->
    pow_aux(Base*Base, Pow, Exp div 2);
pow_aux(Base, Pow, Exp) ->
    pow_aux(Base, Base * Pow, Exp - 1).

pow(Base, Exp) -> pow_aux(Base, 1, Exp).

%% Number to password and back conversion

num_to_pass_aux(_N, 0, Pass) -> Pass;
num_to_pass_aux(N, Digit, Pass) ->
    num_to_pass_aux(N div 26, Digit - 1, [$a + N rem 26 | Pass]).

num_to_pass(N) -> num_to_pass_aux(N, ?PASS_LEN, []).

pass_to_num(Pass) ->
    lists:foldl(fun (C, Num) -> Num * 26 + C - $a end, 0, Pass).

%% Hex string to Number

hex_char_to_int(N) ->
    if (N >= $0) and (N =< $9) -> N - $0;
       (N >= $a) and (N =< $f) -> N - $a + 10;
       (N >= $A) and (N =< $F) -> N - $A + 10;
       true                    -> throw({not_hex, [N]})
    end.

int_to_hex_char(N) ->
    if (N >= 0)  and (N < 10) -> $0 + N;
       (N >= 10) and (N < 16) -> $A + (N - 10);
       true                   -> throw({out_of_range, N})
    end.

hex_string_to_num(Hex_Str) ->
    lists:foldl(fun(Hex, Num) -> Num*16 + hex_char_to_int(Hex) end, 0, Hex_Str).

num_to_hex_string_aux(0, Str) -> Str;
num_to_hex_string_aux(N, Str) ->
    num_to_hex_string_aux(N div 16,
                          [int_to_hex_char(N rem 16) | Str]).

num_to_hex_string(0) -> "0";
num_to_hex_string(N) -> num_to_hex_string_aux(N, []).

%% Progress bar runs in its own process

progress_loop(N, Bound, Time1) ->
    receive
        {stopMain, Pid} ->
            Pid ! stop;
            %ok;
        {stopMainNotFound, HashL, Pid} -> 
            Pid ! {not_sol_found, HashL},
            ok;
        {progress_report, Checked, Time2} ->
            N2 = N + Checked,
            Full_N = N2 * ?BAR_SIZE div Bound,
            Full = lists:duplicate(Full_N, $=),
            Empty = lists:duplicate(?BAR_SIZE - Full_N, $-),
            Passps = (?UPDATE_BAR_GAP/(Time2-Time1))*1000000000,
            io:format("\r[~s~s] ~.2f% Pass/sec: ~.2f ", [Full, Empty, N2/Bound*100,Passps]),
            progress_loop(N2, Bound,Time2)
    end.

%% break_md5/2 iterates checking the possible passwords

break_md5([],_,_,_,MainPid)-> % All hashes have been found
    MainPid ! end_proc,
    ok;
break_md5(HashL, N, N, _, MainPid) -> 
    MainPid ! {not_found, HashL},  % Checked every possible password 
    ok;
break_md5(HashL, N, Bound, Progress_Pid, MainPid) ->
    receive
        {update, NewList} -> 
            break_md5(NewList, N, Bound, Progress_Pid, MainPid);
        stop -> ok
    after 0 ->
        if N rem ?UPDATE_BAR_GAP == 0 ->
                Progress_Pid ! {progress_report, ?UPDATE_BAR_GAP, erlang:monotonic_time(nanosecond)};
           true ->
                ok
        end,
        Pass = num_to_pass(N),
        Hash = crypto:hash(md5, Pass),
        Num_Hash = binary:decode_unsigned(Hash),
        case lists:member(Num_Hash, HashL) of
            true->
                io:format("\e[2K\r~.16B: ~s~n", [Num_Hash, Pass]),
                MainPid ! {delHash, lists:delete(Num_Hash,HashL)},
                break_md5(lists:delete(Num_Hash,HashL), N+1, Bound, Progress_Pid, MainPid);
            false ->
                break_md5(HashL, N+1, Bound, Progress_Pid, MainPid)
        end
    end.

aux(Progress_Pid, Progress_list, Finished)-> 
    receive
        {not_sol_found, HashL} -> {not_found, HashL};
        stop -> ok;
        {delHash, NewList}->
            Fun = fun(Pid) -> Pid ! {update, NewList} end,
            lists:foreach(Fun, Progress_list),
            aux(Progress_Pid, Progress_list, Finished);
        end_proc -> 
            if Finished == (?NUMBER_PROCS-1) -> % If all processes are finished, it also finish main proc
                Progress_Pid ! {stopMain, self()},
                aux(Progress_Pid, Progress_list, Finished);
            true ->  
                aux(Progress_Pid, Progress_list, Finished+1)
            end;
        {not_found, HashL} -> 
            if Finished == (?NUMBER_PROCS-1) -> % If at least one hash is not found
                Progress_Pid ! {stopMainNotFound, HashL, self()},
                aux(Progress_Pid, Progress_list, Finished);
            true -> 
                aux(Progress_Pid, Progress_list, Finished+1)
            end
    end.


start_procs(0,_,_,_, Progress_Pid, Progress_list, Finished)->
    aux(Progress_Pid, Progress_list, Finished);


start_procs(Procs_num, Num_HashL, Lowerbound, Upperbound, Progress_Pid, Progress_list, Finished)->
    Start = Upperbound div ?NUMBER_PROCS * (Procs_num-1),
    End   = Upperbound div ?NUMBER_PROCS * (Procs_num),
    Ppid  = spawn(?MODULE, break_md5,[Num_HashL, Start, End, Progress_Pid, self()]),
    start_procs(Procs_num-1, Num_HashL, Lowerbound, Upperbound, Progress_Pid, [Ppid | Progress_list], Finished).


%% Break a hash

break_md5(Hash) ->
    break_md5s([Hash]).

%% Break a Hash Lists

break_md5s(HashL) -> 
    Bound = pow(26, ?PASS_LEN),
    Progress_Pid = spawn(?MODULE, progress_loop, [0, Bound, erlang:monotonic_time(nanosecond)]),
    Num_HashL = lists:map(fun hex_string_to_num/1, HashL),
    %Res = break_md5(Num_HashL, 0, Bound, Progress_Pid),
    Res = start_procs(?NUMBER_PROCS, Num_HashL, 0, Bound, Progress_Pid, [], 0),
    Res.
