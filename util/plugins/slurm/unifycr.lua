--
--   UnifyCR option to start unifycr daemon from within SLURM

--
--  If user requested --unifycr unifycr == true
--
unifycr_enabled = nil

function opt_handler (val, optarg, isremote)
    unifycr_enabled = true
    return SPANK.SUCCESS
end

function slurm_spank_init (spank)
    --
    -- Register --unifycr option
    --
    local enable_unifycr_opt = {
       name =    "unifycr",
       usage =   "start and stop unifycr daemon " ..
                 "on nodes of job before and after job runs",
           cb =      "opt_handler"
    }
        local rc, err = spank:register_option (enable_unifycr_opt)
        if not rc then
            return SPANK.log_error ("Failed to register option")
        end
        return SPANK.SUCCESS
end

local function start_unifycr ()
    local status = os.execute("/g/g0/sikich1/UnifyCR/install/bin/unifycrd &")
        return status == 0 and SPANK.SUCCESS or SPANK.FAILURE
end

local function stop_unifycr ()
    local status = os.execute("pkill /g/g0/sikich1/UnifyCR/install/bin/unifycrd")
        return status == 0 and SPANK.SUCCESS or SPANK.FAILURE
end

function slurm_spank_task_init (spank)

        if spank.context == "remote" then
                if unifycr_enabled then
                        return start_unifycr ()
                else
                        return SPANK.SUCCESS
                end
        end

        return SPANK.SUCCESS
end

function slurm_spank_task_exit (spank)

        if spank.context == "remote" then
                if unifycr_enabled then
                        return stop_unifycr ()
                else
                        return SPANK.SUCCESS
                end
        end

        return SPANK.SUCCESS
end
